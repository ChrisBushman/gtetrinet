#!/usr/bin/env python3
"""Rewrite every non-system absolute-path dylib dependency of a binary to
@executable_path/../Frameworks/<name>, copying each dylib into a
Frameworks dir the first time it's needed and recursing into its own
dependencies too. Mirrors AmuletsArmor-Bundle's scripts/merge.py
relink_dylibs() -- deliberately not dylibbundler, which mapped this
binary's several distinct build-time absolute rpaths all onto the same
@executable_path/../Frameworks/ value, producing duplicate LC_RPATH load
commands that a modern dyld refuses to load at all.
"""
import os
import re
import shutil
import subprocess
import sys

SYSTEM_DYLIB_PREFIXES = ("/usr/lib/", "/System/Library/")
ALREADY_RELINKED_PREFIX = "@executable_path/../Frameworks/"

# SDL_image never *links* its image-format backends -- it loads each one
# lazily the first time it decodes that format, via dlopen() of a bare,
# unversioned leaf name (see `strings libSDL_image...dylib`). relink_dylibs()
# above only ever walks real LC_LOAD_DYLIB entries, so it copies in the
# versioned libpng16.16.dylib (as a transitive dep of freetype) but never
# under the plain "libpng.dylib" name SDL_image actually dlopen()s. dyld
# resolves a slash-less dlopen name against DYLD_LIBRARY_PATH only, never
# @executable_path -- so the bundle's launcher script (Build/MacOSX-PPC/
# bundle-launcher.sh) sets DYLD_LIBRARY_PATH to this Frameworks dir, and
# here we make sure the bare names it'll ask for actually exist in it, as
# symlinks onto whatever versioned copy landed.
SDL_IMAGE_DLOPEN_ALIASES = (
    "libpng.dylib",
    "libjpeg.dylib",
    "libtiff.dylib",
    "libwebp.dylib",
)


def run(cmd):
    print("+", " ".join(cmd), flush=True)
    subprocess.run(cmd, check=True)


def otool_deps(binary_path):
    result = subprocess.run(["otool", "-L", binary_path], capture_output=True, text=True, check=True)
    deps = []
    for line in result.stdout.splitlines()[1:]:
        line = line.strip()
        if not line:
            continue
        deps.append(line.split(" (", 1)[0])
    return deps


def otool_rpaths(binary_path):
    result = subprocess.run(["otool", "-l", binary_path], capture_output=True, text=True, check=True)
    rpaths = []
    lines = result.stdout.splitlines()
    for i, line in enumerate(lines):
        if line.strip() == "cmd LC_RPATH":
            path_line = lines[i + 2].strip()
            # format: "path <value> (offset N)"
            rpaths.append(path_line[len("path "):].rsplit(" (offset", 1)[0])
    return rpaths


def resolve_dep_path(dep_path, rpaths, loader_dir):
    """Homebrew dylibs sometimes reference their own siblings via
    @rpath/name.dylib (not an absolute path) -- e.g. libwebp.dylib
    depending on @rpath/libsharpyuv.0.dylib, resolved via an
    @loader_path/../lib rpath entry (Homebrew's Cellar layout: the
    referencing dylib and its sibling both sit in the same lib/). otool
    -L can't resolve any of this itself (it's not dyld), so it's done
    by hand here: substitute @loader_path with the directory the
    referencing dylib actually lives in (loader_dir -- NOT
    Contents/Frameworks/, since by the time a copy's own dependencies
    are being resolved it's already been copied there, but its
    original Cellar siblings weren't) before trying each rpath entry.
    Returns None if unresolvable (already-relinked @executable_path
    refs, or a genuine dangling @rpath)."""
    if not dep_path.startswith("@rpath/"):
        return dep_path
    name = dep_path[len("@rpath/"):]
    for rpath in rpaths:
        if rpath.startswith("@loader_path"):
            rpath = loader_dir + rpath[len("@loader_path"):]
        candidate = os.path.normpath(os.path.join(rpath, name))
        if os.path.exists(candidate):
            return candidate
    return None


def relink_dylibs(binary_path, frameworks_dir, loader_dir=None, _seen=None):
    if _seen is None:
        _seen = set()
    if loader_dir is None:
        loader_dir = os.path.dirname(os.path.realpath(binary_path))
    rpaths = otool_rpaths(binary_path)

    for dep_path in otool_deps(binary_path):
        if dep_path.startswith(SYSTEM_DYLIB_PREFIXES) or dep_path.startswith(ALREADY_RELINKED_PREFIX):
            continue

        resolved_path = resolve_dep_path(dep_path, rpaths, loader_dir)
        if resolved_path is None:
            print(f"warning: {dep_path} (needed by {binary_path}) has no matching rpath, leaving reference unresolved",
                  file=sys.stderr)
            continue

        name = os.path.basename(resolved_path)
        new_ref = f"@executable_path/../Frameworks/{name}"
        run(["install_name_tool", "-change", dep_path, new_ref, binary_path])

        dst_dylib = os.path.join(frameworks_dir, name)
        if os.path.exists(dst_dylib):
            continue
        if not os.path.exists(resolved_path):
            print(f"warning: {name} (needed by {binary_path}) not found at {resolved_path}, leaving reference unresolved",
                  file=sys.stderr)
            continue
        os.makedirs(frameworks_dir, exist_ok=True)
        shutil.copy2(resolved_path, dst_dylib, follow_symlinks=True)
        os.chmod(dst_dylib, 0o755)
        run(["install_name_tool", "-id", new_ref, dst_dylib])
        if name not in _seen:
            _seen.add(name)
            relink_dylibs(dst_dylib, frameworks_dir, os.path.dirname(resolved_path), _seen)


def create_dlopen_aliases(frameworks_dir):
    """For each bare name SDL_image dlopen()s (libpng.dylib, ...), symlink
    it onto the versioned copy already in frameworks_dir (libpng16.16.dylib,
    ...), if one is present and the bare name doesn't already exist. The
    symlink is relative so it survives the bundle being moved around."""
    if not os.path.isdir(frameworks_dir):
        return
    present = os.listdir(frameworks_dir)
    for alias in SDL_IMAGE_DLOPEN_ALIASES:
        alias_path = os.path.join(frameworks_dir, alias)
        if os.path.lexists(alias_path):
            continue
        stem = alias[: -len(".dylib")]  # e.g. "libpng"
        # Match versioned siblings like libpng16.16.dylib / libjpeg.9.dylib:
        # same stem, then a version (digit or '.') before the .dylib suffix.
        pattern = re.compile(r"^" + re.escape(stem) + r"[.0-9].*\.dylib$")
        matches = sorted(name for name in present if pattern.match(name))
        if not matches:
            continue
        target = matches[0]
        print(f"+ ln -s {target} {alias_path}", flush=True)
        os.symlink(target, alias_path)


if __name__ == "__main__":
    binary_path, frameworks_dir = sys.argv[1], sys.argv[2]
    relink_dylibs(binary_path, frameworks_dir)
    create_dlopen_aliases(frameworks_dir)
