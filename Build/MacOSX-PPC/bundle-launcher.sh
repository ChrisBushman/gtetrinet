#!/bin/sh
# Installed into the .app as Contents/MacOS/gtetrinet (the bundle's
# CFBundleExecutable) -- so this is what Finder/LaunchServices actually
# runs. The real Mach-O binary lives beside it as gtetrinet-bin.
#
# Its whole job: put the bundle's own Contents/Frameworks on
# DYLD_LIBRARY_PATH, then exec the real binary. SDL_image loads
# libpng/libjpeg/... lazily via dlopen() of a bare leaf name
# ("libpng.dylib"), which dyld resolves against DYLD_LIBRARY_PATH only,
# never @executable_path -- so without this the bundled libpng is never
# found on a clean (no-Tigerbrew) Tiger box and the app aborts loading
# its first PNG. dyld reads DYLD_LIBRARY_PATH only at launch, so it has
# to be set here, in a wrapper that runs *before* the binary starts --
# not by the binary re-exec'ing itself, which under LaunchServices drops
# the GUI session and the window never appears. relink_dylibs.py seeds
# Frameworks with the bare-name symlinks (libpng.dylib -> libpng16.16).
here=$(cd "$(dirname "$0")" && pwd)
export DYLD_LIBRARY_PATH="$here/../Frameworks${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}"
exec "$here/gtetrinet-bin" "$@"
