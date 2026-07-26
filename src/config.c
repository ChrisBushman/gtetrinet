/*
 *  GTetrinet
 *  Copyright (C) 1999, 2000, 2001, 2002, 2003  Ka-shu Wong (kswong@zip.com.au)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <glib.h>
#include <SDL.h>

#include "gtet_config.h"
#include "client.h"
#include "tetrinet.h"
#include "sound.h"
#include "misc.h"
#include "tetris.h"
#include "fields.h"

/* gtetrinet.h/partyline.h are still GTK-typed (gtetrinet.c/partyline.c
 * haven't been ported yet), so they aren't included here -- just the
 * plain declarations this file actually needs from them. Restore the
 * real includes once those files are ported in a later pass. */
extern int gamemode;
extern int timestampsenable;
extern int list_enabled;
extern void partyline_show_channel_list (int enabled);

char blocksfile[1024];
int bsize;

GString *currenttheme = NULL;

static char *soundkeys[S_NUM] = {
    "Drop",
    "Solidify",
    "LineClear",
    "Tetris",
    "Rotate",
    "SpecialLine",
    "YouWin",
    "YouLose",
    "Message",
    "GameStart",
};

/* See gtet_config.h for why plain SDL keycodes (rather than GDK keyvals)
 * are used, and why no case-normalization step is needed. */
int defaultkeys[K_NUM] = {
    SDLK_RIGHT,
    SDLK_LEFT,
    SDLK_UP,
    SDLK_RCTRL,
    SDLK_DOWN,
    SDLK_SPACE,
    SDLK_d,
    SDLK_t,
    SDLK_1,
    SDLK_2,
    SDLK_3,
    SDLK_4,
    SDLK_5,
    SDLK_6
};

int keys[K_NUM];

/* --- ini-based settings storage, replacing GSettings/dconf --- */

static GKeyFile *cfg = NULL;
static char cfg_path[1024];

static void
config_ensure_loaded (void)
{
    char *dir;

    if (cfg != NULL)
        return;

    dir = g_build_filename (g_get_user_config_dir (), "gtetrinet", NULL);
    g_mkdir_with_parents (dir, 0700);
    GTET_O_STRCPY (cfg_path, dir);
    GTET_O_STRCAT (cfg_path, G_DIR_SEPARATOR_S "config.ini");
    g_free (dir);

    cfg = g_key_file_new ();
    /* Missing file is fine -- every config_get_* below has its own
     * default for a key (or whole file) that isn't there yet, exactly
     * like g_settings_get_* always returning the schema's default for
     * a never-set key. */
    g_key_file_load_from_file (cfg, cfg_path, G_KEY_FILE_NONE, NULL);
}

static void
config_save (void)
{
    gchar *data;
    gsize len;

    data = g_key_file_to_data (cfg, &len, NULL);
    g_file_set_contents (cfg_path, data, len, NULL);
    g_free (data);
}

/* Returned string must be g_free()'d by the caller; NULL if unset. */
static gchar *
config_get_string (const char *group, const char *key)
{
    config_ensure_loaded ();
    return g_key_file_get_string (cfg, group, key, NULL);
}

static void
config_set_string (const char *group, const char *key, const char *val)
{
    config_ensure_loaded ();
    g_key_file_set_string (cfg, group, key, val);
    config_save ();
}

static gboolean
config_get_bool (const char *group, const char *key, gboolean def)
{
    GError *err = NULL;
    gboolean v;

    config_ensure_loaded ();
    v = g_key_file_get_boolean (cfg, group, key, &err);
    if (err) {
        g_error_free (err);
        return def;
    }
    return v;
}

/* --- key-name <-> SDL keycode lookup, replacing gdk_keyval_from_name ---
 *
 * gdk_keyval_from_name()/gdk_keyval_to_lower() don't exist outside GDK,
 * and SDL_GetKeyFromName() (the SDL2 equivalent) doesn't exist in real
 * SDL 1.2, which the OS X Tiger/PPC and IRIX ports use -- so this is a
 * small hand-written table instead, covering every key a player could
 * plausibly rebind to. Names match what the original GDK-based config
 * used where they overlap (Right/Left/Up/Down/space/Control_R), so an
 * old hand-edited value still resolves the same way. */
struct keyname { const char *name; int keycode; };

static const struct keyname keynames[] = {
    {"Right", SDLK_RIGHT}, {"Left", SDLK_LEFT}, {"Up", SDLK_UP}, {"Down", SDLK_DOWN},
    {"space", SDLK_SPACE},
    {"Control_R", SDLK_RCTRL}, {"Control_L", SDLK_LCTRL},
    {"Shift_R", SDLK_RSHIFT}, {"Shift_L", SDLK_LSHIFT},
    {"Alt_R", SDLK_RALT}, {"Alt_L", SDLK_LALT},
    {"Tab", SDLK_TAB}, {"Return", SDLK_RETURN}, {"Escape", SDLK_ESCAPE},
    {"a", SDLK_a}, {"b", SDLK_b}, {"c", SDLK_c}, {"d", SDLK_d}, {"e", SDLK_e},
    {"f", SDLK_f}, {"g", SDLK_g}, {"h", SDLK_h}, {"i", SDLK_i}, {"j", SDLK_j},
    {"k", SDLK_k}, {"l", SDLK_l}, {"m", SDLK_m}, {"n", SDLK_n}, {"o", SDLK_o},
    {"p", SDLK_p}, {"q", SDLK_q}, {"r", SDLK_r}, {"s", SDLK_s}, {"t", SDLK_t},
    {"u", SDLK_u}, {"v", SDLK_v}, {"w", SDLK_w}, {"x", SDLK_x}, {"y", SDLK_y},
    {"z", SDLK_z},
    {"0", SDLK_0}, {"1", SDLK_1}, {"2", SDLK_2}, {"3", SDLK_3}, {"4", SDLK_4},
    {"5", SDLK_5}, {"6", SDLK_6}, {"7", SDLK_7}, {"8", SDLK_8}, {"9", SDLK_9},
    {NULL, 0}
};

/* Returns 0 (never a valid SDL keycode for these bindings) if name isn't
 * recognized -- caller falls back to the compiled-in default. */
static int
keyname_to_code (const char *name)
{
    int i;
    for (i = 0; keynames[i].name != NULL; i++)
        if (strcmp (keynames[i].name, name) == 0)
            return keynames[i].keycode;
    return 0;
}

void load_theme (const gchar *theme_dir)
{
  /* load the theme */
  g_string_assign(currenttheme, theme_dir);
  config_loadtheme (theme_dir);

  /* update the fields */
  fields_page_destroy_contents ();
  fields_cleanup ();
  fields_init ();
  fields_page_new ();
  fieldslabelupdate();
  if (ingame)
  {
    tetrinet_redrawfields ();
  }
}

/* themedir is assumed to have a trailing slash */
void config_loadtheme (const gchar *themedir)
{
    char buf[1024], *p;
    int i;
    GKeyFile *keyfile;
    GError *err = NULL;

    GTET_O_STRCPY(buf, themedir);
    GTET_O_STRCAT(buf, "theme.cfg");

    keyfile = g_key_file_new ();
    if (!g_key_file_load_from_file (keyfile, buf, 0, &err))
      goto bad_theme;

    p = g_key_file_get_string (keyfile, "Theme", "Name", &err);
    if (!p)
      goto bad_theme;
    g_free (p);

    p = g_key_file_get_string (keyfile, "Graphics", "Blocks", NULL);
    if (!p)
      p = g_strdup("blocks.png");

    GTET_O_STRCPY(blocksfile, themedir);
    GTET_O_STRCAT(blocksfile, p);
    g_free (p);
    bsize = g_key_file_get_integer (keyfile, "Graphics", "BlockSize", &err);
    if (err)
    {
      bsize = 16;
      g_error_free (err);
      err = NULL;
    }

    for (i = 0; i < S_NUM; i ++) {
        p = g_key_file_get_string (keyfile, "Sounds", soundkeys[i], NULL);
        if (p) {
            GTET_O_STRCPY(soundfiles[i], themedir);
            GTET_O_STRCAT(soundfiles[i], p);
            g_free (p);
        }
        else
            soundfiles[i][0] = 0;
    }

    sound_cache ();

    g_key_file_unref (keyfile);

    return;

 bad_theme:
    {
      /* Was a GTK modal warning dialog -- dialogs.c (not yet ported)
       * owns the app's dialog/messagebox machinery going forward, so
       * for now this just logs, matching fields_init()'s own fallback
       * message for the equivalent "can't load this theme" case. */
      fprintf (stderr, "Warning: theme '%s' does not have a name, reverting to default.\n", themedir);
      g_key_file_unref (keyfile);
      g_string_assign(currenttheme, DEFAULTTHEME);
      config_loadtheme (currenttheme->str);
    }
}

/* Arrggh... all these params are sizeof() == 1024 ... this needs a real fix */
int config_getthemeinfo (char *themedir, char *name, char *author, char *desc)
{
    char buf[1024];
    char *p = NULL;
    GKeyFile *keyfile;

    GTET_O_STRCPY (buf, themedir);
    GTET_O_STRCAT (buf, "theme.cfg");

    keyfile = g_key_file_new ();
    if (!g_key_file_load_from_file (keyfile, buf, 0, NULL)) {
        g_key_file_unref (keyfile);
        return -1;
    }

    p = g_key_file_get_string (keyfile, "Theme", "Name", NULL);
    if (p == 0) {
        g_key_file_unref (keyfile);
        return -1;
    }
    else {
        if (name) GTET_STRCPY(name, p, 1024);
        g_free (p);
    }
    if (author) {
        if ((p = g_key_file_get_string (keyfile, "Theme", "Author", NULL))) {
            GTET_STRCPY(author, p, 1024);
            g_free (p);
        } else {
            author[0] = 0;
        }
    }
    if (desc) {
        if ((p = g_key_file_get_string (keyfile, "Theme", "Description", NULL))) {
            GTET_STRCPY(desc, p, 1024);
            g_free (p);
        } else {
            desc[0] = 0;
        }
    }

    g_key_file_unref (keyfile);

    return 0;
}

void config_loadconfig (void)
{
    gchar *p;

    /* get the other sound options */
    soundenable = config_get_bool ("General", "sound-enable", TRUE);
    if (soundenable) {
        sound_cache ();
    }

    /* get the player nickname */
    p = config_get_string ("General", "player-nickname");
    if (p) {
        GTET_O_STRCPY(nick, p);
        g_free(p);
    }

    /* get the server name */
    p = config_get_string ("General", "server");
    if (p) {
        GTET_O_STRCPY(server, p);
        g_free(p);
    }

    /* get the team name */
    p = config_get_string ("General", "player-team");
    if (p) {
        GTET_O_STRCPY(team, p);
        g_free(p);
    }

    /* get the game mode */
    gamemode = config_get_bool ("General", "gamemode", FALSE);

    /* Get the timestamp option. */
    timestampsenable = config_get_bool ("General", "partyline-enable-timestamps", FALSE);

    /* Get the channel list option */
    list_enabled = config_get_bool ("General", "partyline-enable-channel-list", TRUE);

    partyline_show_channel_list(list_enabled);
}

void config_loadconfig_themes (void)
{
    gchar *p;

    currenttheme = g_string_sized_new(100);

    /* get the current theme */
    p = config_get_string ("Theme", "directory");
    /* if there is no theme configured, then we fallback to DEFAULTTHEME */
    if (!p || !p[0])
    {
      g_string_assign(currenttheme, DEFAULTTHEME);
      config_set_string ("Theme", "directory", currenttheme->str);
    }
    else
      g_string_assign(currenttheme, p);
    if (p) g_free (p);

    /* add trailing slash if none exists */
    if (currenttheme->str[currenttheme->len - 1] != '/')
      g_string_append_c(currenttheme, '/');

    load_theme (currenttheme->str);
}

void config_loadconfig_keys (void)
{
    /* {ini-file key name, GTetrinetKeys index} -- replaces 14 nearly-
     * identical g_settings_get_string()+gdk_keyval_from_name() blocks
     * with one data-driven loop. */
    static const struct { const char *cfgkey; int keyidx; } keybindings[] = {
        {"right",        K_RIGHT},
        {"left",         K_LEFT},
        {"rotate-right", K_ROTRIGHT},
        {"rotate-left",  K_ROTLEFT},
        {"down",         K_DOWN},
        {"drop",         K_DROP},
        {"discard",      K_DISCARD},
        {"message",      K_GAMEMSG},
        {"special1",     K_SPECIAL1},
        {"special2",     K_SPECIAL2},
        {"special3",     K_SPECIAL3},
        {"special4",     K_SPECIAL4},
        {"special5",     K_SPECIAL5},
        {"special6",     K_SPECIAL6},
    };
    int i;

    for (i = 0; i < K_NUM; i++) {
        gchar *p = config_get_string ("Keys", keybindings[i].cfgkey);
        int code = 0;

        if (p) {
            code = keyname_to_code (p);
            g_free (p);
        }
        keys[keybindings[i].keyidx] = code ? code : defaultkeys[keybindings[i].keyidx];
    }
}

/* --- setters, used by dialogs.c's connect/team/preferences dialogs to
 * persist what the player changes (replacing direct g_settings_set_*
 * calls, since GSettings is gone -- see config_ensure_loaded() above). */

void config_set_server (const char *server)
{
    config_set_string ("General", "server", server);
}

void config_set_nickname (const char *nick)
{
    config_set_string ("General", "player-nickname", nick);
}

void config_set_team (const char *team)
{
    config_set_string ("General", "player-team", team);
}

void config_set_gamemode (int mode)
{
    config_ensure_loaded ();
    g_key_file_set_boolean (cfg, "General", "gamemode", mode ? TRUE : FALSE);
    config_save ();
}

void config_set_sound_enable (int enable)
{
    config_ensure_loaded ();
    g_key_file_set_boolean (cfg, "General", "sound-enable", enable ? TRUE : FALSE);
    config_save ();
}

void config_set_timestamps_enable (int enable)
{
    config_ensure_loaded ();
    g_key_file_set_boolean (cfg, "General", "partyline-enable-timestamps", enable ? TRUE : FALSE);
    config_save ();
}

void config_set_channel_list_enable (int enable)
{
    config_ensure_loaded ();
    g_key_file_set_boolean (cfg, "General", "partyline-enable-channel-list", enable ? TRUE : FALSE);
    config_save ();
}

void config_set_theme_directory (const char *dir)
{
    config_set_string ("Theme", "directory", dir);
}

/* Reverse of keyname_to_code(), used when persisting a newly bound key. */
static const char *
code_to_keyname (int code)
{
    int i;
    for (i = 0; keynames[i].name != NULL; i++)
        if (keynames[i].keycode == code)
            return keynames[i].name;
    return NULL;
}

void config_set_key (int keyidx, int keycode)
{
    static const char *cfgkeys[K_NUM] = {
        "right", "left", "rotate-right", "rotate-left", "down", "drop",
        "discard", "message", "special1", "special2", "special3",
        "special4", "special5", "special6",
    };
    const char *name;

    if (keyidx < 0 || keyidx >= K_NUM)
        return;
    keys[keyidx] = keycode;
    name = code_to_keyname (keycode);
    if (name)
        config_set_string ("Keys", cfgkeys[keyidx], name);
}
