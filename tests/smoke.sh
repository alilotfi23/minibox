#!/usr/bin/env bash
set -euo pipefail
BIN="${1:-./build/minibox}"
ROOTFS="${2:-./rootfs}"

test -x "$BIN"
test -d "$ROOTFS"

# Smoke test: run a trivial command from inside the rootfs.
sudo "$BIN" run "$ROOTFS" /bin/true
echo "minibox smoke test passed"
