# Native Makefile for IRIX 6.5 / SGI O2 (IP32, MIPS R12000).
# Run this ON the O2 (TGCware's GCC 4.5.2 is the toolchain -- see
# AmuletsArmor's Build/IRIX-O2/README.md for the same toolchain setup),
# or via remote-build.sh from a modern dev machine over SSH.
#
# Dependencies, all via Nekoware (/usr/nekoware): SDL 1.2, SDL_image,
# SDL_mixer, SDL_net were already installed on this box; SDL_ttf, glib2
# (+ its own deps: libffi, gettext/libintl, libiconv), and freetype2
# (SDL_ttf's dependency) were added via Nekoware .tardist packages during
# this port. There is no pkg-config binary on this box, so glib/freetype
# flags are hardcoded below (read directly off
# /usr/nekoware/lib32/pkgconfig/glib-2.0.pc and `freetype-config`) rather
# than discovered at configure time.
#
# Nekoware's own directory split is NOT a MIPS ABI split here -- `file`
# confirms libSDL*, libglib-2.0, and libfreetype are all ELF N32, so they
# link together fine despite living under lib/ vs lib32/ (an Nekoware
# packaging-generation quirk, not an ABI boundary).
#
# Login shell on IRIX is csh, and IRIX's own /bin/sh has no `~` expansion
# -- this Makefile only assumes POSIX make behavior, not the login shell.

# $(abspath) needs GNU Make 3.81+; this box's GNU Make may be older (see
# AmuletsArmor's IRIX Makefile) -- $(shell cd .. && pwd) is the portable
# equivalent.
REPO_ROOT := $(shell cd ../.. && pwd)
SRC_DIR   := $(REPO_ROOT)/src

BUILD_DIR := build

CC         := /usr/tgcware/gcc45/bin/gcc
SDL_CONFIG := /usr/nekoware/bin/sdl-config
FT_CONFIG  := /usr/nekoware/bin/freetype-config

SDL_CFLAGS := $(shell $(SDL_CONFIG) --cflags)
SDL_LIBS   := $(shell $(SDL_CONFIG) --libs) -L/usr/nekoware/lib -lSDL_image -lSDL_ttf -lSDL_mixer

FT_CFLAGS  := $(shell $(FT_CONFIG) --cflags)
FT_LIBS    := $(shell $(FT_CONFIG) --libs)

# Hardcoded from /usr/nekoware/lib32/pkgconfig/glib-2.0.pc (no pkg-config
# binary on this box to read it automatically).
GLIB_CFLAGS := -I/usr/nekoware/include/glib-2.0 -I/usr/nekoware/lib32/glib-2.0/include
GLIB_LIBS   := -L/usr/nekoware/lib32 -lglib-2.0 -lintl -lpthread -liconv

SOURCES := $(SRC_DIR)/sched.c \
           $(SRC_DIR)/client.c \
           $(SRC_DIR)/tetrinet.c \
           $(SRC_DIR)/tetris.c \
           $(SRC_DIR)/fields.c \
           $(SRC_DIR)/misc.c \
           $(SRC_DIR)/config.c \
           $(SRC_DIR)/sound.c \
           $(SRC_DIR)/winlist.c \
           $(SRC_DIR)/partyline.c \
           $(SRC_DIR)/commands.c \
           $(SRC_DIR)/dialogs.c \
           $(SRC_DIR)/gtetrinet.c

OBJS := $(addprefix $(BUILD_DIR)/,$(notdir $(SOURCES:.c=.c.o)))

vpath %.c $(SRC_DIR)

# VERSION/PACKAGE mirror configure.ac's AC_INIT (gtetrinet, 0.8.0), same
# as the top-level CMakeLists.txt / Build/MacOSX-PPC/Makefile.
# GTETRINET_DATA/GTETPIXMAPSDIR point at this synced repo checkout
# directly -- unless GTET_RELATIVE_DATA_DIR is set (`gmake
# GTET_RELATIVE_DATA_DIR=1`), which points them at "."/"./icons"
# instead, matching CMakeLists.txt's/Build/MacOSX-PPC/Makefile's own
# GTET_RELATIVE_DATA_DIR option. That's for release packaging: a
# downloaded tarball has no reason to still be sitting at this
# machine's checkout path, and (unlike the macOS bundle case) there's
# no bundle-relative chdir needed here either -- IRIX has no app-bundle
# concept, so this only works if the release package's own run-o2.csh
# launcher cd's into its own directory first (it does). Off by default
# since this Makefile's normal dev/test use (remote-build.sh + running
# directly from build/) needs the absolute path instead.
ifdef GTET_RELATIVE_DATA_DIR
GTET_DATA_DIR    := .
GTET_PIXMAPS_DIR := ./icons
else
GTET_DATA_DIR    := $(REPO_ROOT)
GTET_PIXMAPS_DIR := $(REPO_ROOT)/icons
endif

DEFINES := -DVERSION='"0.8.0"' -DPACKAGE='"gtetrinet"' \
           -DGTETRINET_DATA='"$(GTET_DATA_DIR)"' \
           -DGTETPIXMAPSDIR='"$(GTET_PIXMAPS_DIR)"'

INCLUDES := -I$(SRC_DIR)

OPT_FLAGS ?= -g -O2
STD_FLAGS := -std=gnu99

DEPFLAGS   := -MMD -MP
ALL_CFLAGS := $(OPT_FLAGS) $(STD_FLAGS) $(DEFINES) $(SDL_CFLAGS) $(GLIB_CFLAGS) $(FT_CFLAGS) $(INCLUDES) $(DEPFLAGS)

TARGET := $(BUILD_DIR)/gtetrinet

.PHONY: all clean run

all: $(TARGET)

# This box's only GNU Make is 3.74 (1996), which predates order-only
# prerequisites ("|", added in 3.81) entirely -- a `%.c.o: %.c |
# $(BUILD_DIR)` rule silently fails to register on it (confirmed: "No
# rule to make target" for every object file). $(shell mkdir -p ...)
# sidesteps the problem completely: it runs once, at parse time, via
# plain shell-function expansion that every make version supports.
$(shell mkdir -p $(BUILD_DIR))

$(TARGET): $(OBJS)
	$(CC) -o $@ $(OBJS) $(SDL_LIBS) $(GLIB_LIBS) $(FT_LIBS)

$(BUILD_DIR)/%.c.o: %.c
	$(CC) $(ALL_CFLAGS) -c $< -o $@

-include $(OBJS:.o=.d)

clean:
	rm -rf $(BUILD_DIR)

run: all
	$(TARGET)
