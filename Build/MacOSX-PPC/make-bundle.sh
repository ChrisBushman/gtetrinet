#!/bin/bash
# Build GTetrinet.app and GTetrinet-OSX-PPC.dmg for PowerPC / Mac OS X
# 10.4 "Tiger". Run this ON the Tiger box (it needs that box's own gcc/
# gmake, python3, png2icns, and -- crucially -- the old cctools
# install_name_tool, which is the only one that can rewrite PPC Mach-O;
# modern Apple install_name_tool errors out on it). remote-build.sh
# syncs the repo there; then: cd Build/MacOSX-PPC && ./make-bundle.sh
#
# Output (override via env): OUT_APP and OUT_DMG.
#
# The bundle's CFBundleExecutable is bundle-launcher.sh (installed as
# Contents/MacOS/gtetrinet); the real binary is Contents/MacOS/
# gtetrinet-bin. See bundle-launcher.sh for why the DYLD_LIBRARY_PATH
# setup lives in a wrapper rather than the binary.
set -euo pipefail
export PATH=/usr/local/bin:$PATH

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$HERE/../.." && pwd)"
VOL="GTetrinet ${GTET_VERSION:-0.8.0}"
STAGE="${STAGE:-/tmp/gtet-bundle}"
OUT_DMG="${OUT_DMG:-/tmp/GTetrinet-OSX-PPC.dmg}"
APP="$STAGE/$VOL/GTetrinet.app"

cd "$REPO_ROOT"

echo "==> clean build (GTET_RELATIVE_DATA_DIR=1)"
gmake -C Build/MacOSX-PPC GTET_RELATIVE_DATA_DIR=1 clean all >/dev/null

echo "==> assemble bundle skeleton"
rm -rf "$STAGE" "$OUT_DMG"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Frameworks" "$APP/Contents/Resources"

# real binary + wrapper launcher (the latter is CFBundleExecutable)
cp Build/MacOSX-PPC/build/gtetrinet "$APP/Contents/MacOS/gtetrinet-bin"
cp "$HERE/bundle-launcher.sh"       "$APP/Contents/MacOS/gtetrinet"
chmod +x "$APP/Contents/MacOS/gtetrinet-bin" "$APP/Contents/MacOS/gtetrinet"

echo "==> relink dylibs into Frameworks (+ bare-name aliases)"
python3 scripts/relink_dylibs.py "$APP/Contents/MacOS/gtetrinet-bin" "$APP/Contents/Frameworks"

echo "==> copy Resources"
cp -R data   "$APP/Contents/Resources/data"
cp -R icons  "$APP/Contents/Resources/icons"
cp -R themes "$APP/Contents/Resources/themes"
cp gtetrinet.png "$APP/Contents/Resources/gtetrinet.png"
scripts/make_icns.sh gtetrinet.png "$APP/Contents/Resources/icon.icns" >/dev/null

echo "==> write Info.plist"
cat > "$APP/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleExecutable</key>
	<string>gtetrinet</string>
	<key>CFBundleIdentifier</key>
	<string>net.sf.gtetrinet</string>
	<key>CFBundleName</key>
	<string>GTetrinet</string>
	<key>CFBundleDisplayName</key>
	<string>GTetrinet</string>
	<key>CFBundleVersion</key>
	<string>0.8.0</string>
	<key>CFBundleShortVersionString</key>
	<string>0.8.0</string>
	<key>CFBundlePackageType</key>
	<string>APPL</string>
	<key>CFBundleSignature</key>
	<string>????</string>
	<key>CFBundleInfoDictionaryVersion</key>
	<string>6.0</string>
	<key>CFBundleIconFile</key>
	<string>icon.icns</string>
</dict>
</plist>
PLIST

echo "==> stage README.md"
cp "$HERE/dmg-README.md" "$STAGE/$VOL/README.md"

echo "==> create HFS+/APM compressed dmg (mounts on real Tiger hardware)"
hdiutil create -srcfolder "$STAGE/$VOL" -volname "$VOL" \
    -fs HFS+ -layout SPUD -format UDZO -ov "$OUT_DMG" >/dev/null

echo "==> done: $OUT_DMG"
ls -l "$OUT_DMG"
