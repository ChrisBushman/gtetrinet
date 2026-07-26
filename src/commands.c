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

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <glib.h>

#include "client.h"
#include "tetrinet.h"
#include "partyline.h"
#include "misc.h"
#include "commands.h"
#include "dialogs.h"

/* Forward declaration instead of pulling in the still-GTK gtetrinet.h
 * (same approach config.c already uses for its own forward-declared
 * externs). */
extern void destroymain (void);

static T_menuitem menu_items[CMD_COUNT] = {
    { CMD_CONNECT,     "Connect to server...", 1, 1 },
    { CMD_DISCONNECT,  "Disconnect",            0, 0 },
    { CMD_CHANGE_TEAM, "Change team...",        1, 1 },
    { CMD_START_GAME,  "Start game",            1, 0 },
    { CMD_PAUSE_GAME,  "Pause game",            1, 0 },
    { CMD_END_GAME,    "End game",              0, 0 },
    { CMD_PREFERENCES, "Preferences",           1, 1 },
    { CMD_ABOUT,       "About",                 1, 1 },
    { CMD_EXIT,        "Quit",                  1, 1 },
};

static void set_visible (T_command_id id, int v) { menu_items[id].visible = v; }
static void set_enabled (T_command_id id, int v) { menu_items[id].enabled = v; }

void commands_init (void)
{
    /* Matches make_menus()'s ACTION_HIDE ("EndGame") / ACTION_HIDE
       ("Disconnect") -- the initial, not-yet-connected state. */
    set_visible (CMD_END_GAME, 0);
    set_visible (CMD_DISCONNECT, 0);
}

int commands_menu_count (void)
{
    return CMD_COUNT;
}

const T_menuitem *commands_menu_item (int index)
{
    if (index < 0 || index >= CMD_COUNT)
        return NULL;
    return &menu_items[index];
}

/* callbacks */

static void connect_command (void)
{
    connectdialog_new ();
}

static void disconnect_command (void)
{
    client_disconnect ();
}

static void team_command (void)
{
    teamdialog_new ();
}

static void start_command (void)
{
    char buf[22];

    g_snprintf (buf, sizeof (buf), "%i %i", 1, playernum);
    client_outmessage (OUT_STARTGAME, buf);
}

static void end_command (void)
{
    char buf[22];

    g_snprintf (buf, sizeof (buf), "%i %i", 0, playernum);
    client_outmessage (OUT_STARTGAME, buf);
}

static void pause_command (void)
{
    char buf[22];

    g_snprintf (buf, sizeof (buf), "%i %i", paused ? 0 : 1, playernum);
    client_outmessage (OUT_PAUSE, buf);
}

static void preferences_command (void)
{
    prefdialog_new ();
}

static void about_command (void)
{
    aboutdialog_new ();
}

void commands_activate (T_command_id id)
{
    if (id < 0 || id >= CMD_COUNT)
        return;
    if (!menu_items[id].visible || !menu_items[id].enabled)
        return;

    switch (id) {
    case CMD_CONNECT:     connect_command ();     break;
    case CMD_DISCONNECT:  disconnect_command ();  break;
    case CMD_CHANGE_TEAM: team_command ();        break;
    case CMD_START_GAME:  start_command ();       break;
    case CMD_PAUSE_GAME:  pause_command ();       break;
    case CMD_END_GAME:    end_command ();         break;
    case CMD_PREFERENCES: preferences_command (); break;
    case CMD_ABOUT:       about_command ();       break;
    case CMD_EXIT:        destroymain ();         break;
    default: break;
    }
}

void show_connect_button (void)
{
    set_visible (CMD_DISCONNECT, 0);
    set_visible (CMD_CONNECT, 1);
}

void show_disconnect_button (void)
{
    set_visible (CMD_CONNECT, 0);
    set_visible (CMD_DISCONNECT, 1);
}

void show_stop_button (void)
{
    set_visible (CMD_START_GAME, 0);
    set_visible (CMD_END_GAME, 1);
}

void show_start_button (void)
{
    set_visible (CMD_END_GAME, 0);
    set_visible (CMD_START_GAME, 1);
}

/* the following function enable/disable things */

void commands_checkstate (void)
{
    if (connected) {
        set_enabled (CMD_CONNECT, 0);
        set_enabled (CMD_DISCONNECT, 1);
    }
    else {
        set_enabled (CMD_CONNECT, 1);
        set_enabled (CMD_DISCONNECT, 0);
    }
    if (moderator) {
        if (ingame) {
            set_enabled (CMD_START_GAME, 0);
            set_enabled (CMD_PAUSE_GAME, 1);
            set_enabled (CMD_END_GAME, 1);
        }
        else {
            set_enabled (CMD_START_GAME, 1);
            set_enabled (CMD_PAUSE_GAME, 0);
            set_enabled (CMD_END_GAME, 0);
        }
    }
    else {
        set_enabled (CMD_START_GAME, 0);
        set_enabled (CMD_PAUSE_GAME, 0);
        set_enabled (CMD_END_GAME, 0);
    }
    if (ingame || spectating) {
        set_enabled (CMD_CHANGE_TEAM, 0);
    }
    else {
        set_enabled (CMD_CHANGE_TEAM, 1);
    }

    partyline_connectstatus (connected);

    if (ingame) partyline_status ("Game in progress");
    else if (connected) {
        char buf[256];
        GTET_O_STRCPY (buf, "Connected to\n");
        GTET_STRCAT (buf, server, sizeof (buf));
        partyline_status (buf);
    }
    else partyline_status ("Not connected");
}
