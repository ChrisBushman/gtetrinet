/* Standalone correctness test for commands.c -- not part of the app
 * build. Compile+run manually:
 *   cc test_commands.c commands.c -I. \
 *     $(pkg-config --cflags --libs glib-2.0 sdl2) \
 *     -o /tmp/test_commands && /tmp/test_commands
 */
#include <stdio.h>
#include <string.h>
#include <SDL.h>
#include "client.h"
#include "tetrinet.h"
#include "partyline.h"
#include "misc.h"
#include "dialogs.h"
#include "commands.h"

int connected;
int ingame, playing, paused;
int moderator, spectating;
int playernum;
char server[128];

int connect_calls, disconnect_calls, team_calls, prefs_calls, about_calls, exit_calls;
char last_outmessage[300];
enum outmsg_type last_outmsg_type;

void client_disconnect (void) { disconnect_calls++; }
void client_outmessage (enum outmsg_type msgtype, char *str)
{
    last_outmsg_type = msgtype;
    GTET_STRCPY (last_outmessage, str, sizeof (last_outmessage));
}
void partyline_connectstatus (int status) { (void) status; }
char last_status[256];
void partyline_status (char *status) { GTET_STRCPY (last_status, status, sizeof (last_status)); }
void connectdialog_new (void) { connect_calls++; }
void teamdialog_new (void) { team_calls++; }
void prefdialog_new (void) { prefs_calls++; }
void aboutdialog_new (void) { about_calls++; }
void destroymain (void) { exit_calls++; }

static int fails = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf ("FAIL: %s\n", msg); fails++; } \
    else printf ("ok: %s\n", msg); \
} while (0)

static const T_menuitem *find (T_command_id id)
{
    return commands_menu_item ((int) id);
}

int main (void)
{
    commands_init ();

    CHECK (find (CMD_END_GAME)->visible == 0, "EndGame starts hidden");
    CHECK (find (CMD_DISCONNECT)->visible == 0, "Disconnect starts hidden");
    CHECK (find (CMD_CONNECT)->visible == 1, "Connect starts visible");

    /* --- checkstate: disconnected, non-moderator, not in game --- */
    connected = 0; moderator = 0; ingame = 0; spectating = 0;
    commands_checkstate ();
    CHECK (find (CMD_CONNECT)->enabled == 1, "disconnected: Connect enabled");
    CHECK (find (CMD_DISCONNECT)->enabled == 0, "disconnected: Disconnect disabled");
    CHECK (find (CMD_START_GAME)->enabled == 0, "non-moderator: StartGame disabled");
    CHECK (find (CMD_CHANGE_TEAM)->enabled == 1, "not in game: ChangeTeam enabled");
    CHECK (strcmp (last_status, "Not connected") == 0, "status text: Not connected");

    /* --- checkstate: connected moderator, not in game --- */
    connected = 1; moderator = 1; ingame = 0;
    GTET_STRCPY (server, "example.com:31457", sizeof (server));
    commands_checkstate ();
    CHECK (find (CMD_CONNECT)->enabled == 0, "connected: Connect disabled");
    CHECK (find (CMD_DISCONNECT)->enabled == 1, "connected: Disconnect enabled");
    CHECK (find (CMD_START_GAME)->enabled == 1, "moderator, not in game: StartGame enabled");
    CHECK (find (CMD_PAUSE_GAME)->enabled == 0, "moderator, not in game: PauseGame disabled");
    CHECK (strstr (last_status, "example.com:31457") != NULL, "status text includes server");

    /* --- checkstate: connected moderator, in game --- */
    ingame = 1;
    commands_checkstate ();
    CHECK (find (CMD_START_GAME)->enabled == 0, "moderator, in game: StartGame disabled");
    CHECK (find (CMD_PAUSE_GAME)->enabled == 1, "moderator, in game: PauseGame enabled");
    CHECK (find (CMD_END_GAME)->enabled == 1, "moderator, in game: EndGame enabled");
    CHECK (find (CMD_CHANGE_TEAM)->enabled == 0, "in game: ChangeTeam disabled");
    CHECK (strcmp (last_status, "Game in progress") == 0, "status text: Game in progress");

    /* --- show_*_button visibility toggles --- */
    show_disconnect_button ();
    CHECK (find (CMD_CONNECT)->visible == 0 && find (CMD_DISCONNECT)->visible == 1,
           "show_disconnect_button swaps visibility");
    show_connect_button ();
    CHECK (find (CMD_CONNECT)->visible == 1 && find (CMD_DISCONNECT)->visible == 0,
           "show_connect_button swaps back");
    show_stop_button ();
    CHECK (find (CMD_START_GAME)->visible == 0 && find (CMD_END_GAME)->visible == 1,
           "show_stop_button swaps visibility");
    show_start_button ();
    CHECK (find (CMD_START_GAME)->visible == 1 && find (CMD_END_GAME)->visible == 0,
           "show_start_button swaps back");

    /* --- activation dispatch, including the visible/enabled guard --- */
    playernum = 3; paused = 0;
    find (CMD_START_GAME); /* just to be explicit about which item we're testing */
    /* moderator+not-in-game state from above still has StartGame enabled+visible */
    ingame = 0;
    commands_checkstate ();
    commands_activate (CMD_START_GAME);
    CHECK (last_outmsg_type == OUT_STARTGAME && strcmp (last_outmessage, "1 3") == 0,
           "activating StartGame sends '1 <playernum>'");

    commands_activate (CMD_PREFERENCES);
    CHECK (prefs_calls == 1, "activating Preferences calls prefdialog_new");

    commands_activate (CMD_ABOUT);
    CHECK (about_calls == 1, "activating About calls aboutdialog_new");

    commands_activate (CMD_CHANGE_TEAM);
    CHECK (team_calls == 1, "activating ChangeTeam calls teamdialog_new");

    /* CMD_DISCONNECT is currently hidden (we called show_start_button,
       which doesn't touch Disconnect, but the earlier show_connect_button
       call left Disconnect hidden) -- activating a hidden item must be a
       silent no-op, not fall through to disconnect_command(). */
    CHECK (find (CMD_DISCONNECT)->visible == 0, "sanity: Disconnect is currently hidden");
    commands_activate (CMD_DISCONNECT);
    CHECK (disconnect_calls == 0, "activating a hidden menu item is a no-op");

    commands_activate (CMD_EXIT);
    CHECK (exit_calls == 1, "activating Exit calls destroymain");

    printf ("%s\n", fails ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED");
    return fails ? 1 : 0;
}
