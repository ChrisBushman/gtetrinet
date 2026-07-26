# Native Makefile for PowerPC / Mac OS X 10.4 "Tiger".
# Run this ON the Tiger box (Tigerbrew's gcc is the primary supported
# toolchain), or via remote-build.sh from a modern dev machine over SSH.
#
# Dependencies (all via Tigerbrew -- `brew install sdl sdl_net sdl_image
# sdl_ttf sdl_mixer glib`): SDL 1.2 (this port uses SDL 1.2 on Tiger/IRIX,
# SDL2 on Windows/modern macOS -- see gtet_config.h), SDL_image, SDL_ttf,
# SDL_mixer, and glib-2.0 (the core-logic files, ported from a GTK app,
# still use GString/GKeyFile/GScanner/g_snprintf -- this is NOT a GTK
# dependency, just glib itself; nothing here links GTK).

REPO_ROOT := $(abspath ../..)
SRC_DIR   := $(REPO_ROOT)/src

BUILD_DIR := build

CC          ?= gcc
ARCH        ?= ppc
MACOSX_VERSION_MIN ?= 10.4
SDKROOT     ?=
SDL_CONFIG  ?= sdl-config
PKG_CONFIG  ?= pkg-config

SDL_CFLAGS  := $(shell $(SDL_CONFIG) --cflags)
# SDL_image/SDL_ttf/SDL_mixer are SDL 1.2-era side-libs with no -config
# script of their own; their headers install alongside SDL.h (already on
# SDL_CFLAGS's -I), so only the extra -l flags are needed here.
SDL_LIBS    := $(shell $(SDL_CONFIG) --libs) -lSDL_image -lSDL_ttf -lSDL_mixer

GLIB_CFLAGS := $(shell $(PKG_CONFIG) --cflags glib-2.0)
GLIB_LIBS   := $(shell $(PKG_CONFIG) --libs glib-2.0)

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
# as the top-level CMakeLists.txt. GTETRINET_DATA/GTETPIXMAPSDIR point at
# this synced repo checkout directly (see CMakeLists.txt's comment for
# why they're two different subpaths, not one).
DEFINES := -DVERSION='"0.8.0"' -DPACKAGE='"gtetrinet"' \
           -DGTETRINET_DATA='"$(REPO_ROOT)"' \
           -DGTETPIXMAPSDIR='"$(REPO_ROOT)/icons"'

ARCHFLAGS := -arch $(ARCH) -mmacosx-version-min=$(MACOSX_VERSION_MIN)
ifneq ($(strip $(SDKROOT)),)
ARCHFLAGS += -isysroot $(SDKROOT)
endif

INCLUDES := -I$(SRC_DIR)

OPT_FLAGS ?= -g -O2
STD_FLAGS := -std=gnu99

# -MMD -MP: emit per-object .d dependency files so a shared-header change
# triggers a rebuild of everything that includes it, not just the one
# translation unit named in the compile line -- see AmuletsArmor's
# equivalent PPC Makefile for the exact failure mode this avoids.
DEPFLAGS   := -MMD -MP
ALL_CFLAGS := $(ARCHFLAGS) $(OPT_FLAGS) $(STD_FLAGS) $(DEFINES) $(SDL_CFLAGS) $(GLIB_CFLAGS) $(INCLUDES) $(DEPFLAGS)

TARGET := $(BUILD_DIR)/gtetrinet

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(ARCHFLAGS) -o $@ $(OBJS) $(SDL_LIBS) $(GLIB_LIBS)

$(BUILD_DIR)/%.c.o: %.c | $(BUILD_DIR)
	$(CC) $(ALL_CFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

-include $(OBJS:.o=.d)

clean:
	rm -rf $(BUILD_DIR)

run: all
	$(TARGET)
