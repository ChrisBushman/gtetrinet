# CMake build for the SDL port (modern macOS / generic Unix with
# pkg-config-discoverable deps, and Windows via MSYS2/MinGW -- see
# .github/workflows/windows-release-build.yml, which builds this same
# file with mingw-w64 packages supplying SDL2/glib instead of hand-
# rolling a separate MSVC project). PPC/Tiger and IRIX use their own
# native Makefiles under Build/<Platform>/ instead (MIPSPro-era/old-gcc
# toolchains and ancient CMake, if any, aren't a good fit). This file
# replaces nothing from the pre-SDL-rewrite autotools setup
# (configure.ac/Makefile.am) -- that still describes the old GTK3+
# GSettings build and is left alone for now, not wired into this target.

cmake_minimum_required(VERSION 3.20)

project(gtetrinet LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

set(GTET_EXE_NAME "gtetrinet")
set(GTET_VERSION "0.8.0")

set(GTET_SOURCES
    src/sched.c
    src/client.c
    src/tetrinet.c
    src/tetris.c
    src/fields.c
    src/misc.c
    src/config.c
    src/sound.c
    src/winlist.c
    src/partyline.c
    src/commands.c
    src/dialogs.c
    src/gtetrinet.c
)

add_executable(${GTET_EXE_NAME} ${GTET_SOURCES})

target_include_directories(${GTET_EXE_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED IMPORTED_TARGET glib-2.0)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
pkg_check_modules(SDL2_IMAGE REQUIRED IMPORTED_TARGET SDL2_image)
pkg_check_modules(SDL2_TTF REQUIRED IMPORTED_TARGET SDL2_ttf)
pkg_check_modules(SDL2_MIXER REQUIRED IMPORTED_TARGET SDL2_mixer)

target_link_libraries(${GTET_EXE_NAME} PRIVATE
    PkgConfig::GLIB
    PkgConfig::SDL2
    PkgConfig::SDL2_IMAGE
    PkgConfig::SDL2_TTF
    PkgConfig::SDL2_MIXER
)

# VERSION/PACKAGE mirror configure.ac's AC_INIT (gtetrinet, 0.8.0) --
# HAVE_CONFIG_H is never defined here, so no source file tries to pull
# these from a generated config.h; they're supplied directly instead.
# GTETRINET_DATA is the directory that contains this repo's themes/
# (see gtet_config.h's GTETRINET_THEMES) and gtetrinet.png; the bundled
# font lives one level under that, at data/fonts/ (gtetrinet.c already
# accounts for the "data/" prefix, since that's this repo's own
# directory name, unrelated to the constant's own name). GTETPIXMAPSDIR
# is icons/ specifically (team.png/alone.png; see winlist.c).
#
# On Windows this is "." / "./icons" instead of the absolute source
# path: a downloaded release .exe has no reason to still be sitting at
# this machine's build path, and there's no runtime executable-path
# resolution in this codebase to fall back on. Same convention
# AmuletsArmor already uses (plain relative fopen("cosine.dat", ...)
# etc., resources shipped next to the .exe in Exe/) -- launching by
# double-click or from a shell in that same folder sets the working
# directory to wherever the .exe actually is, which is all this needs.
# macOS/PPC-Tiger/IRIX keep the absolute path: those are deployed by
# this project's own rsync-to-a-fixed-remote-path scripts and run in
# place, not downloaded to an arbitrary folder.
if(WIN32)
    set(GTET_DATA_DIR ".")
    set(GTET_PIXMAPS_DIR "./icons")
else()
    set(GTET_DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    set(GTET_PIXMAPS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/icons")
endif()

target_compile_definitions(${GTET_EXE_NAME} PRIVATE
    VERSION="${GTET_VERSION}"
    PACKAGE="gtetrinet"
    GTETRINET_DATA="${GTET_DATA_DIR}"
    GTETPIXMAPSDIR="${GTET_PIXMAPS_DIR}"
)

if(APPLE OR UNIX)
    # Homebrew's SDL2/glib dylibs aren't always on the default runtime
    # search path (varies Intel /usr/local vs Apple Silicon /opt/homebrew);
    # bake in whatever pkg-config actually resolved so the binary runs
    # without needing DYLD_LIBRARY_PATH/LD_LIBRARY_PATH set by hand --
    # same reasoning as AmuletsArmor's top-level CMakeLists.txt.
    set(GTET_RPATH_DIRS
        ${GLIB_LIBRARY_DIRS}
        ${SDL2_LIBRARY_DIRS}
        ${SDL2_IMAGE_LIBRARY_DIRS}
        ${SDL2_TTF_LIBRARY_DIRS}
        ${SDL2_MIXER_LIBRARY_DIRS}
    )
    list(REMOVE_DUPLICATES GTET_RPATH_DIRS)

    set_target_properties(${GTET_EXE_NAME} PROPERTIES
        BUILD_RPATH "${GTET_RPATH_DIRS}"
        INSTALL_RPATH "${GTET_RPATH_DIRS}"
    )
endif()

if(GTET_WARNINGS_AS_ERRORS)
    target_compile_options(${GTET_EXE_NAME} PRIVATE -Werror)
endif()
