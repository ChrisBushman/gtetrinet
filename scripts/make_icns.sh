#!/usr/bin/env bash
# Builds a .icns containing the classic pre-Leopard raw-bitmap icon
# formats (is32/il32/ih32/it32 + masks) that Tiger's Finder actually
# understands, plus modern PNG-based sizes for contemporary macOS.
#
# `iconutil` (Apple's own current tool, used nowhere in this repo
# anymore) and recent `sips`/ImageMagick versions only emit the newer
# PNG-based icon family codes -- confirmed by decomposing their output
# and finding no is32/il32/it32/*8mk chunks at all, which is why an
# icon built with any of them doesn't show up in Tiger's Finder despite
# being a structurally valid modern .icns.
#
# A from-scratch reimplementation of the classic format's PackBits
# variant was tried and discarded: it round-tripped through its own
# paired decoder correctly but produced garbage when decoding a
# known-good reference icon (and vice versa), meaning its PackBits
# framing didn't actually match Apple's real encoding. Use `png2icns`
# (part of libicns: `brew install libicns`) instead -- a real,
# independently-implemented, widely-used encoder, verified here by
# round-tripping its output back through `icns2png` (a different
# codepath in the same library) and confirming the decoded image
# matches the source.
#
# Usage: ./make_icns.sh <source.png> <output.icns>
set -euo pipefail

if ! command -v png2icns >/dev/null 2>&1; then
    echo "error: png2icns not found -- brew install libicns" >&2
    exit 1
fi

SRC="$1"
OUT="$2"
TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT

for size in 16 32 48 128 256 512; do
    sips -z "$size" "$size" "$SRC" --out "$TMPDIR/icon_$size.png" >/dev/null
done

png2icns "$OUT" \
    "$TMPDIR/icon_16.png" \
    "$TMPDIR/icon_32.png" \
    "$TMPDIR/icon_48.png" \
    "$TMPDIR/icon_128.png" \
    "$TMPDIR/icon_256.png" \
    "$TMPDIR/icon_512.png"
