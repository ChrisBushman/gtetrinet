#!/usr/bin/env bash
# Sync this repo to a PowerPC Mac OS X Tiger box over SSH and build it there.
#
# Usage:
#   GTET_PPC_HOST=aa-tiger ./Build/MacOSX-PPC/remote-build.sh [make-target...]
#
# Env vars:
#   GTET_PPC_HOST   (required) ssh destination, e.g. "user@10.0.0.42" or a
#                   ~/.ssh/config Host alias.
#   GTET_PPC_PATH   remote directory to sync into
#                   (default: ~/gtet-ppc-build/gtetrinet)
#   GTET_PPC_PORT   ssh port (default: 22)
#   GTET_PPC_MAKE   make binary to invoke remotely (default: auto-detect
#                   Tigerbrew's gmake, falling back to the system make --
#                   Tiger's own /usr/bin/make is GNU Make 3.80, whose
#                   order-only-prerequisite support is too buggy for this
#                   Makefile's `| $(BUILD_DIR)` rule).
#
# Any extra arguments are passed through to `make` on the remote box, e.g.:
#   ./Build/MacOSX-PPC/remote-build.sh clean all
#   ./Build/MacOSX-PPC/remote-build.sh run

set -euo pipefail

if [ -z "${GTET_PPC_HOST:-}" ]; then
    echo "error: set GTET_PPC_HOST to the ssh destination of the Tiger box (user@host)" >&2
    exit 1
fi

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
REMOTE_PATH="${GTET_PPC_PATH:-~/gtet-ppc-build/gtetrinet}"
SSH_PORT="${GTET_PPC_PORT:-22}"
MAKE_TARGETS=("$@")
if [ "${#MAKE_TARGETS[@]}" -eq 0 ]; then
    MAKE_TARGETS=(all)
fi

echo "==> Syncing $REPO_ROOT to $GTET_PPC_HOST:$REMOTE_PATH"
ssh -p "$SSH_PORT" "$GTET_PPC_HOST" "mkdir -p $REMOTE_PATH"
rsync -avz --delete \
    -e "ssh -p $SSH_PORT" \
    --exclude='.git/' \
    --exclude='build/' \
    --exclude='out/' \
    --exclude='Build/MacOSX-PPC/build/' \
    "$REPO_ROOT/" "$GTET_PPC_HOST:$REMOTE_PATH/"

# Non-interactive SSH sessions don't source .bash_profile/.profile, so
# Tigerbrew's /usr/local/bin (gmake, sdl-config, pkg-config, ...) isn't on
# PATH by default here even though it is in an interactive login shell.
if [ -z "${GTET_PPC_MAKE:-}" ]; then
    GTET_PPC_MAKE=$(ssh -p "$SSH_PORT" "$GTET_PPC_HOST" \
        'PATH="/usr/local/bin:$PATH"; command -v gmake || command -v make')
fi

echo "==> Building on $GTET_PPC_HOST ($GTET_PPC_MAKE ${MAKE_TARGETS[*]})"
ssh -p "$SSH_PORT" "$GTET_PPC_HOST" \
    "export PATH=/usr/local/bin:\$PATH; cd $REMOTE_PATH/Build/MacOSX-PPC && $GTET_PPC_MAKE ${MAKE_TARGETS[*]}"

echo "==> Done. Binary at $REMOTE_PATH/Build/MacOSX-PPC/build/gtetrinet on $GTET_PPC_HOST"
