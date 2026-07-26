#!/usr/bin/env bash
# Sync this repo to an SGI O2 (IRIX 6.5) box over SSH and build it there.
#
# Usage:
#   GTET_O2_HOST=aa-o2 ./Build/IRIX-O2/remote-build.sh [make-target...]
#
# Env vars:
#   GTET_O2_HOST   (required) ssh destination, e.g. "bushmac@10.0.0.42" or a
#                  ~/.ssh/config Host alias.
#   GTET_O2_PATH   remote directory to sync into
#                  (default: ~/gtet-o2-build/gtetrinet)
#   GTET_O2_PORT   ssh port (default: 22)
#   GTET_O2_MAKE   make binary to invoke remotely (default: the only GNU
#                  Make actually on this box, from a bundled cross-toolchain
#                  kit -- there is no Nekoware make package, and IRIX's own
#                  /usr/bin/make is MIPSPro's, not GNU. Same box/path as
#                  AmuletsArmor's IRIX build.)
#
# No rsync exists on this box (Nekoware doesn't package it, and IRIX's own
# /usr/bin/rsync is an unrelated legacy tool with the same name). Syncing
# goes over a plain tar-over-ssh pipe instead, same as AmuletsArmor's IRIX
# build -- unlike that repo, gtetrinet's runtime assets (themes/, icons/,
# a ~700KB bundled font) are small enough that there's no equivalent
# "exclude the huge asset directory by default" concern here.
#
# Any extra arguments are passed through to `make` on the remote box, e.g.:
#   ./Build/IRIX-O2/remote-build.sh clean all
#   ./Build/IRIX-O2/remote-build.sh run

set -euo pipefail

if [ -z "${GTET_O2_HOST:-}" ]; then
    echo "error: set GTET_O2_HOST to the ssh destination of the O2 (user@host, or a ~/.ssh/config alias)" >&2
    exit 1
fi

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
REMOTE_PATH="${GTET_O2_PATH:-~/gtet-o2-build/gtetrinet}"
SSH_PORT="${GTET_O2_PORT:-22}"
GTET_O2_MAKE="${GTET_O2_MAKE:-/usr/people/bushmac/sh-SOA960904-hms/bin/make}"

MAKE_TARGETS=("$@")
if [ "${#MAKE_TARGETS[@]}" -eq 0 ]; then
    MAKE_TARGETS=(all)
fi

echo "==> Syncing $REPO_ROOT to $GTET_O2_HOST:$REMOTE_PATH"
ssh -p "$SSH_PORT" "$GTET_O2_HOST" "mkdir -p $REMOTE_PATH"
# Plain tar, no compression: IRIX's own /bin/tar is SysV-style with no
# gzip integration, and there's no guarantee gzip/GNU tar exists remotely
# either. LAN-local, so the extra bytes don't matter.
# COPYFILE_DISABLE=1: stop macOS tar from emitting AppleDouble sidecar
# files (._foo) and PaxHeader entries for extended attributes -- IRIX's
# tar doesn't understand them and they'd just be clutter over there.
COPYFILE_DISABLE=1 tar cf - -C "$REPO_ROOT" \
    --exclude='.git' \
    --exclude='build' \
    --exclude='out' \
    --exclude='Build/MacOSX-PPC/build' \
    --exclude='Build/IRIX-O2/build' \
    . | ssh -p "$SSH_PORT" "$GTET_O2_HOST" "cd $REMOTE_PATH && tar xf -"

echo "==> Building on $GTET_O2_HOST ($GTET_O2_MAKE ${MAKE_TARGETS[*]})"
ssh -p "$SSH_PORT" "$GTET_O2_HOST" \
    "cd $REMOTE_PATH/Build/IRIX-O2 && $GTET_O2_MAKE ${MAKE_TARGETS[*]}"

echo "==> Done. Binary at $REMOTE_PATH/Build/IRIX-O2/build/gtetrinet on $GTET_O2_HOST"
