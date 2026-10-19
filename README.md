# minibox

A small educational Linux container-runtime prototype in C++20.

## Current scope

- user, PID, mount, UTS, and IPC namespaces
- parent/child synchronization while UID/GID mappings are installed
- private mount propagation
- bind-mounted root filesystem and `pivot_root`
- private `/proc` mount
- `PR_SET_NO_NEW_PRIVS`
- command execution and exit-status forwarding

## Important security limitations

This is a learning prototype, **not a production container runtime or a safe boundary for hostile workloads**.

It does not yet implement:
- seccomp syscall filtering
- cgroup v2 CPU, memory, and PID limits
- capability bounding/drop policy
- a read-only root filesystem and mount allowlist
- network namespace setup or network isolation
- robust cleanup on every failure path, logging, or signal forwarding
- a complete PID 1 init/reaper

Do not run untrusted workloads with this prototype. Use a mature runtime (for example, rootless Podman) for real isolation.

## Build

```sh
cmake -S . -B build
cmake --build build -j
```

## Prepare a root filesystem

The rootfs must contain the requested executable and its shared libraries. For a quick Debian/Ubuntu test, a debootstrap-created directory is convenient:

```sh
sudo debootstrap --variant=minbase stable ./rootfs http://deb.debian.org/debian
```

If your distro's `debootstrap` uses a different suite or mirror, adjust those values.

## Run

```sh
sudo ./build/minibox run "$PWD/rootfs" /bin/sh
```

Inside the container, try:

```sh
hostname
readlink /proc/self
cat /proc/self/status
exit
```

Unprivileged user namespaces may be disabled by host policy. The rootfs must be prepared beforehand, and the kernel must support the requested namespaces and `pivot_root`.

## Next engineering milestones

1. Add explicit capability dropping and a configurable seccomp profile.
2. Create a cgroup v2 child cgroup and enforce `memory.max`, `pids.max`, and `cpu.max`.
3. Add mount allowlists, read-only rootfs mode, `/dev` setup, and safe bind-mount handling.
4. Add `CLONE_NEWNET` with a deliberate no-network or veth configuration.
5. Add pidfd-based supervision, signal forwarding, and PID 1 child reaping.
6. Add integration tests in a disposable VM.
