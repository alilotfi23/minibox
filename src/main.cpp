#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <stdexcept>
#include <vector>

#include <fcntl.h>
#include <sched.h>
#include <sys/mount.h>
#include <sys/syscall.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
constexpr std::size_t kStackSize = 1024 * 1024;

[[noreturn]] void die(const std::string& what) {
    std::cerr << "minibox: " << what << ": " << std::strerror(errno) << '\n';
    std::exit(EXIT_FAILURE);
}

void check(int rc, const std::string& what) {
    if (rc == -1) die(what);
}

void write_file(const std::string& path, const std::string& value) {
    int fd = open(path.c_str(), O_WRONLY | O_CLOEXEC);
    if (fd == -1) throw std::runtime_error("open " + path + ": " + std::strerror(errno));
    std::size_t done = 0;
    while (done < value.size()) {
        ssize_t n = write(fd, value.data() + done, value.size() - done);
        if (n == -1 && errno == EINTR) continue;
        if (n <= 0) {
            int saved = errno;
            close(fd);
            errno = saved;
            throw std::runtime_error("write " + path + ": " + std::strerror(errno));
        }
        done += static_cast<std::size_t>(n);
    }
    if (close(fd) == -1) throw std::runtime_error("close " + path + ": " + std::strerror(errno));
}

struct Config {
    std::string rootfs;
    std::string hostname = "minibox";
    std::vector<char*> argv;
    int sync_read = -1;
};

int child_main(void* ptr) {
    auto& cfg = *static_cast<Config*>(ptr);

    // Parent configures uid_map/gid_map before the child continues.
    char token = 0;
    ssize_t n;
    do { n = read(cfg.sync_read, &token, 1); } while (n == -1 && errno == EINTR);
    if (n != 1 || token != 'G') {
        std::cerr << "minibox: parent failed to configure user namespace\n";
        return 125;
    }
    close(cfg.sync_read);

    if (setresgid(0, 0, 0) == -1 || setresuid(0, 0, 0) == -1) {
        std::perror("setresuid/setresgid");
        return 125;
    }

    if (mount(nullptr, "/", nullptr, MS_REC | MS_PRIVATE, nullptr) == -1) {
        std::perror("make mounts private");
        return 125;
    }

    // Bind-mount the rootfs onto itself so pivot_root can use it as a mount.
    if (mount(cfg.rootfs.c_str(), cfg.rootfs.c_str(), nullptr, MS_BIND | MS_REC, nullptr) == -1) {
        std::perror("bind rootfs");
        return 125;
    }
    if (chdir(cfg.rootfs.c_str()) == -1) {
        std::perror("chdir rootfs");
        return 125;
    }

    // Keep the old root temporarily, then detach it after pivot_root.
    if (mkdir(".minibox-oldroot", 0700) == -1 && errno != EEXIST) {
        std::perror("mkdir old root");
        return 125;
    }
    if (syscall(SYS_pivot_root, ".", ".minibox-oldroot") == -1) {
        std::perror("pivot_root");
        return 125;
    }
    if (chdir("/") == -1) {
        std::perror("chdir /");
        return 125;
    }
    if (umount2("/.minibox-oldroot", MNT_DETACH) == -1) {
        std::perror("detach old root");
        return 125;
    }
    if (rmdir("/.minibox-oldroot") == -1) {
        std::perror("remove old root mountpoint");
        return 125;
    }

    if (sethostname(cfg.hostname.c_str(), cfg.hostname.size()) == -1) {
        std::perror("sethostname");
        return 125;
    }

    // A minimal proc mount is useful for tools such as ps. This is inside
    // the new mount/PID namespaces, so it does not expose the host's /proc.
    if (mkdir("/proc", 0555) == -1 && errno != EEXIST) {
        std::perror("mkdir /proc");
        return 125;
    }
    if (mount("proc", "/proc", "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC, nullptr) == -1) {
        std::perror("mount proc");
        return 125;
    }

    // Prevent execve from granting additional privilege. This is not a
    // substitute for dropping capabilities or installing a seccomp policy.
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == -1) {
        std::perror("PR_SET_NO_NEW_PRIVS");
        return 125;
    }

    execvp(cfg.argv[0], cfg.argv.data());
    std::perror("execvp");
    return errno == ENOENT ? 127 : 126;
}

void map_user(pid_t pid, uid_t uid, gid_t gid) {
    const std::string proc = "/proc/" + std::to_string(pid) + "/";
    // Unprivileged gid mapping requires disabling setgroups first.
    write_file(proc + "setgroups", "deny");
    write_file(proc + "uid_map", "0 " + std::to_string(uid) + " 1\n");
    write_file(proc + "gid_map", "0 " + std::to_string(gid) + " 1\n");
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 4 || std::string(argv[1]) != "run") {
        std::cerr << "Usage: minibox run ROOTFS COMMAND [ARGS...]\n"
                  << "Example: sudo ./build/minibox run /path/to/rootfs /bin/sh\n";
        return 2;
    }

    char* resolved = realpath(argv[2], nullptr);
    if (!resolved) die("realpath rootfs");
    std::string rootfs(resolved);
    std::free(resolved);

    struct stat st {};
    if (stat(rootfs.c_str(), &st) == -1) die("stat rootfs");
    if (!S_ISDIR(st.st_mode)) {
        std::cerr << "minibox: ROOTFS must be a directory\n";
        return 2;
    }

    Config cfg;
    cfg.rootfs = rootfs;
    for (int i = 3; i < argc; ++i) cfg.argv.push_back(argv[i]);
    cfg.argv.push_back(nullptr);

    int sync_pipe[2];
    check(pipe2(sync_pipe, O_CLOEXEC), "pipe2");
    cfg.sync_read = sync_pipe[0];

    std::vector<char> stack(kStackSize);
    int flags = CLONE_NEWUSER | CLONE_NEWPID | CLONE_NEWNS |
                CLONE_NEWUTS | CLONE_NEWIPC | SIGCHLD;
    // Network namespace is intentionally not enabled in this first version:
    // adding it would also require an explicit network setup strategy.
    pid_t child = clone(child_main, stack.data() + stack.size(), flags, &cfg);
    check(child, "clone");

    close(sync_pipe[0]);
    try {
        map_user(child, getuid(), getgid());
    } catch (const std::exception& e) {
        std::cerr << "minibox: " << e.what() << '\n';
        kill(child, SIGKILL);
        close(sync_pipe[1]);
        waitpid(child, nullptr, 0);
        return 125;
    }

    const char go = 'G';
    if (write(sync_pipe[1], &go, 1) != 1) {
        kill(child, SIGKILL);
        close(sync_pipe[1]);
        waitpid(child, nullptr, 0);
        die("release child");
    }
    close(sync_pipe[1]);

    int status = 0;
    if (waitpid(child, &status, 0) == -1) die("waitpid");
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return 125;
}
