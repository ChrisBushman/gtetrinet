/* Standalone correctness test for config.c's ini-based settings and
 * keyname_to_code lookup -- not part of the app build. Doesn't need to
 * link fields.c/misc.c/sched.c: config_loadconfig_themes() (the only
 * path that touches fields_*) is never called here, so those four
 * fields_* calls inside load_theme() just need stub symbols, not real
 * implementations.
 *
 * config.c's settings are cached in a static GKeyFile* loaded once per
 * process (matching GSettings' own semantics: read once at startup,
 * mutated only through the same API from then on) -- so "does it read
 * defaults with no file" and "does it respect a pre-existing file" are
 * two genuinely different process lifetimes, not two calls in one. Run
 * both phases (see the driver script below), each with $HOME/
 * XDG_CONFIG_HOME pointed at its own scratch directory so neither ever
 * touches a real user's config:
 *
 *   cc test_config.c config.c -I. \
 *     $(pkg-config --cflags --libs glib-2.0 sdl2) \
 *     -DGTETRINET_DATA='"'"$(pwd)/.."'"' \
 *     -o /tmp/test_config
 *
 *   T1=$(mktemp -d)
 *   HOME="$T1" XDG_CONFIG_HOME="$T1/.config" /tmp/test_config defaults
 *   rm -rf "$T1"
 *
 *   T2=$(mktemp -d)
 *   mkdir -p "$T2/.config/gtetrinet"
 *   cat > "$T2/.config/gtetrinet/config.ini" <<EOF
 *   [Keys]
 *   right=l
 *   discard=bogus-unknown-name
 *   [General]
 *   sound-enable=false
 *   player-nickname=TestNick
 *   EOF
 *   HOME="$T2" XDG_CONFIG_HOME="$T2/.config" /tmp/test_config withfile
 *   rm -rf "$T2"
 */
#include <stdio.h>
#include <string.h>
#include <glib.h>
#include <SDL.h>
#include "gtet_config.h"
#include "tetrinet.h"
#include "tetris.h"
#include "misc.h"

/* --- stubs for externs/functions config.c needs but doesn't own --- */
char specialblocks[256];
int specialblocknum;
int ingame, playing, paused;
int gamemode;
int timestampsenable;
int list_enabled;
char server[128];
char team[128], nick[128], specpassword[128];
int connected;

void partyline_show_channel_list (int enabled) { (void) enabled; }
void fieldslabelupdate (void) {}
void tetrinet_redrawfields (void) {}
void sound_cache (void) {}
int soundenable;
char soundfiles[10][1024];

void fields_page_destroy_contents (void) {}
void fields_cleanup (void) {}
int fields_init (void) { return 0; }
void fields_page_new (void) {}

static int fails = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf ("FAIL: %s\n", msg); fails++; } \
    else printf ("ok: %s\n", msg); \
} while (0)

static void
check_defaults (void)
{
    config_loadconfig_keys ();
    CHECK (keys[K_RIGHT] == SDLK_RIGHT, "K_RIGHT defaults to SDLK_RIGHT with no config file");
    CHECK (keys[K_DISCARD] == SDLK_d, "K_DISCARD defaults to SDLK_d with no config file");
    CHECK (keys[K_SPECIAL3] == SDLK_3, "K_SPECIAL3 defaults to SDLK_3 with no config file");

    config_loadconfig ();
    CHECK (soundenable != 0, "sound-enable defaults to true with no config file");
    CHECK (nick[0] == 0, "player-nickname is empty (untouched) with no config file");
}

static void
check_withfile (void)
{
    config_loadconfig_keys ();
    CHECK (keys[K_RIGHT] == SDLK_l, "K_RIGHT correctly rebound to SDLK_l from a pre-existing config file");
    CHECK (keys[K_DISCARD] == SDLK_d,
           "unrecognized key name in config falls back to compiled-in default rather than 0");
    CHECK (keys[K_LEFT] == SDLK_LEFT, "K_LEFT (not present in the written file) still defaults correctly");

    config_loadconfig ();
    CHECK (soundenable == 0, "sound-enable=false read back correctly as a real ini value, not just the default");
    CHECK (strcmp (nick, "TestNick") == 0, "player-nickname read back correctly from a pre-existing config file");
}

int main (int argc, char **argv)
{
    const char *xdg;

    SDL_Init (0);

    if (argc != 2 || (strcmp (argv[1], "defaults") != 0 && strcmp (argv[1], "withfile") != 0)) {
        fprintf (stderr, "usage: %s defaults|withfile\n", argv[0]);
        return 2;
    }

    /* Sanity check we're actually sandboxed before touching anything. */
    xdg = g_getenv ("XDG_CONFIG_HOME");
    CHECK (xdg != NULL && strlen (xdg) > 0, "XDG_CONFIG_HOME is set (sandboxed run)");

    if (strcmp (argv[1], "defaults") == 0)
        check_defaults ();
    else
        check_withfile ();

    printf ("%s\n", fails ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED");
    return fails ? 1 : 0;
}
