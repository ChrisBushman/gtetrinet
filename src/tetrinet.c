
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

/* gtk.h is only still needed here for the one remaining
 * gtk_message_dialog_new() call (a connection-error dialog) --
 * dialogs.c (not yet ported) will own a portable equivalent later. */
#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include <unistd.h>
#include <stdio.h>
#include <ctype.h>

#include "gtetrinet.h"
#include "gtet_config.h"
#include "client.h"
#include "tetrinet.h"
#include "tetris.h"
#include "fields.h"
#include "partyline.h"
#include "winlist.h"
#include "misc.h"
#include "commands.h"
#include "dialogs.h"
#include "sound.h"
#include "string.h"
#include "sched.h"

#define NEXTBLOCKDELAY (gamemode==TETRIFAST?0:1000)
#define DOWNDELAY 100
#define PARTYLINEDELAY1 100
#define PARTYLINEDELAY2 200

/* all the game state variables goes here */
int playernum = 0;
int moderatornum = 0;
char team[128], nick[128], specpassword[128];

#define MAX_PLAYERS 7

char pnumrec = 0;
char btrixgame = 0;

char playernames[MAX_PLAYERS][128];
char teamnames[MAX_PLAYERS][128];
int playerlevels[MAX_PLAYERS];
int playerplaying[MAX_PLAYERS];
int playercount = 0;

char spectatorlist[128][128];
int spectatorcount = 0;

char specialblocks[256];
int specialblocknum = 0;

/*
 * this will have the number of /list commands sended and waiting for answer
 */
int list_issued;

FIELD fields[7];

int moderator; /* are we the moderator ? TRUE : FALSE */
int spectating; /* are we spectating ? TRUE : FALSE */
int tetrix; /* are we connected to a tetrix server ? TRUE : FALSE */

int linecount, level;
int slines, llines; /* lines still to be used for incrementing level, etc */
int ingame, playing, paused;
int gmsgstate;
int bigfieldnum;

/* game options from the server */
int initialstackheight; /* height of random crap */
int initiallevel; /* speed speed level */
int linesperlevel; /* number of lines before you go up a level */
int levelinc; /* amount you go level up, after every linesperlevel */
int speciallines; /* ratio of lines needed for special blocks */
int specialcount; /* multiplier for special blocks to add */
int specialcapacity; /* max number of specials you can have */
int levelaverage; /* flag: should we average the levels across all players */
int classicmode; /* bitflag: does everyone get lines of blocks when you
                  * 2x, 3x or tetris */

/* these are actually cumulative frequency counts */
int blockfreq[7];
int specialfreq[9];

static char blocks[] = "012345acnrsbgqo";

struct sb {
    char *id;
    int block;
    char *info;
};

#define S_ADDALL1      0
#define S_ADDALL2      1
#define S_ADDALL4      2
#define S_ADDLINE      3
#define S_CLEARLINE    4
#define S_NUKEFIELD    5
#define S_CLEARBLOCKS  6
#define S_SWITCH       7
#define S_CLEARSPECIAL 8
#define S_GRAVITY      9
#define S_BLOCKQUAKE   10
#define S_BLOCKBOMB    11

struct sb sbinfo[] = {
    {"cs1", -1, "1 Line Added"},
    {"cs2", -1, "2 Lines Added"},
    {"cs4", -1, "4 Lines Added"},
    {"a",   6,  "Add Line"},
    {"c",   7,  "Clear Line"},
    {"n",   8,  "Nuke Field"},
    {"r",   9,  "Clear Random"},
    {"s",  10,  "Switch Fields"},
    {"b",  11,  "Clear Specials"},
    {"g",  12,  "Block Gravity"},
    {"q",  13,  "Blockquake"},
    {"o",  14,  "Block Bomb"},
    {0, 0, 0}
};

static guint up_chan_list_source;

static void tetrinet_updatelevels (void);
static void tetrinet_setspeciallabel (int sb);
static void tetrinet_dospecial (int from, int to, int type);
static void tetrinet_specialkey (int pnum);
static void tetrinet_shiftline (int l, int d, FIELD field);
static void moderatorupdate (int now);
static void partylineupdate (int now);
static void partylineupdate_join (char *name);
static void partylineupdate_team (char *name, char *team);
static void partylineupdate_leave (char *name);
static void playerlistupdate (void);
static void plinemsg (const char *name, const char *text);
static void plineact (const char *name, const char *text);
static void plinesmsg (const char *name, const char *text);
static void plinesact (const char *name, const char *text);
static char translateblock (char c);
static void clearallfields (void);
static void checkmoderatorstatus (void);
static void speclist_clear (void);
static void speclist_add (char *name);
static void speclist_remove (char *name);

static FIELD sentfield; /* the field that the server thinks we have */

/*
 this function processes incoming messages
 */
void tetrinet_inmessage (enum inmsg_type msgtype, char *data)
{
    int tmp_pnum = 0;
    char buf[1024];
    if (msgtype != IN_PLAYERJOIN && msgtype != IN_PLAYERLEAVE && msgtype != IN_TEAM &&
        msgtype != IN_PLAYERNUM && msgtype != IN_CONNECT && msgtype != IN_F && msgtype != IN_WINLIST) {
        partylineupdate (1);
        moderatorupdate (1);
    }
#ifdef DEBUG
    printf ("%d %s\n", msgtype, data);
#endif
    /* process the message */
    switch (msgtype) {
    case IN_CONNECT:
        list_issued = 0;
        up_chan_list_source = sched_timeout_add (30000, (sched_timer_func) partyline_update_channel_list, NULL);
        partyline_joining_channel ("");
        show_start_button ();
        show_disconnect_button ();
        break;
    case IN_DISCONNECT:
        if (!connected) {
            data = _("Server disconnected");
            goto connecterror;
        }
        if (ingame) tetrinet_endgame ();
        connected = ingame = playing = paused = moderator = tetrix = FALSE;
        commands_checkstate ();
        partyline_playerlist (NULL, NULL, NULL, 0, NULL, 0);
        partyline_namelabel (NULL, NULL);
        playernum = moderatornum = playercount = spectatorcount = 0;
        {
            /* clear player and team lists */
            int i;
            for (i = 0; i <= 6; i ++)
                playernames[i][0] = teamnames[i][0] = 0;
            fieldslabelupdate ();
        }
        sched_timer_remove (up_chan_list_source);
        winlist_clear ();
        fields_attdefclear ();
        fields_gmsgclear ();
        partyline_fmt (_("%c%c*** Disconnected from server"),
                       TETRI_TB_C_DARK_GREEN, TETRI_TB_BOLD);
        partyline_clear_list_channel ();
        partyline_joining_channel (NULL);
        show_connect_button ();
        break;
    case IN_CONNECTERROR:
    connecterror:
        {
            GtkWidget *dialog;
            gchar *data_utf8;
            connectingdialog_destroy ();
            GTET_O_STRCPY (buf, _("Error connecting: "));
            data_utf8 = g_locale_to_utf8 (data, -1, NULL, NULL, NULL);
            GTET_O_STRCAT (buf, data_utf8);
            dialog = gtk_message_dialog_new (NULL, GTK_DIALOG_MODAL,
                                             GTK_MESSAGE_ERROR,
                                             GTK_BUTTONS_OK,
                                             "%s", buf);
            gtk_dialog_run (GTK_DIALOG(dialog));
            gtk_widget_destroy (dialog);
            g_free (data_utf8);
            show_connect_button ();
        }
        break;
    case IN_PLAYERNUM:
        pnumrec = 1;
        tmp_pnum = atoi (data);
        if (tmp_pnum >= MAX_PLAYERS || tmp_pnum < 0)
          break;
        bigfieldnum = playernum = tmp_pnum;
        if (!connected)
        {
            /* we have successfully connected */
            if (spectating) {
                g_snprintf (buf, sizeof(buf), "%d %s", playernum, specpassword);
                client_outmessage (OUT_TEAM, buf);
                client_outmessage (OUT_VERSION, APPNAME"-"APPVERSION);
                partyline_namelabel (nick, NULL);
            }
            /* tell everybody of successful connection */
            client_outmessage (OUT_CONNECTED, NULL);
            /* set up stuff */
            connected = TRUE;
            ingame = playing = paused = FALSE;
            playercount = 0;
            partyline_fmt (_("%c%c*** Connected to server"),
                           TETRI_TB_C_DARK_GREEN, TETRI_TB_BOLD);
            commands_checkstate ();
            connectingdialog_destroy ();
            connectdialog_connected ();
            if (spectating) tetrix = TRUE;
            else tetrix = FALSE;
        }
        if (!spectating) {
            /* If we occupy a previously empty slot increase playercount */
            if (playernames[playernum][0] == 0)
                playercount++;
            /* set own player/team info */
            GTET_O_STRCPY (playernames[playernum], nick);
            GTET_O_STRCPY (teamnames[playernum], team);
            /* send team info */
            g_snprintf (buf, sizeof(buf), "%d %s", playernum, team);
            client_outmessage (OUT_TEAM, buf);
            /* update display */
            playerlistupdate ();
            partyline_namelabel (nick, team);
            fieldslabelupdate ();
            if (connected) {
                checkmoderatorstatus ();
                partylineupdate_join (nick);
                partylineupdate_team (nick, team);
            }
        }
        /* show partyline on successful connect */
        partyline_update_channel_list ();
        show_partyline_page ();
        break;
    case IN_PLAYERJOIN:
        {
            int pnum;
            char *token;
            /* new player has joined */
            token = strtok (data, " ");
            if (token == NULL) break;
            pnum = atoi (token);
            if (pnum >= MAX_PLAYERS || pnum < 0)
              break;
            token = strtok (NULL, "");
            if (token == NULL) break;
            if (playernames[pnum][0] == 0) playercount ++;
            GTET_O_STRCPY (playernames[pnum], token);
            teamnames[pnum][0] = 0;
            playerlistupdate ();
            /* update fields display */
            fieldslabelupdate ();
            /* display */
            partylineupdate_join (playernames[pnum]);
            /* check moderator status */
            checkmoderatorstatus ();
            /* update channel list */
            partyline_update_channel_list ();
            /* send out our field */
            /* if (!spectating) tetrinet_resendfield (); */
        }
        break;
    case IN_PLAYERLEAVE:
        {
            int pnum;
            char *token;
            /* player has left */
            token = strtok (data, " ");
            if (token == NULL) break;
            pnum = atoi (token);
            if (pnum >= MAX_PLAYERS || pnum < 0)
              break;
            if (!playercount)
              break;
            playercount --;
            if (playernames[pnum][0]) {
                /* display */
                partylineupdate_leave (playernames[pnum]);
            }
            /* update playerlist */
            playernames[pnum][0] = 0;
            teamnames[pnum][0] = 0;
            playerplaying[pnum] = 0;
            playerlistupdate ();
            /* update fields display */
            fieldslabelupdate ();
            memset (fields[pnum], 0, FIELDHEIGHT*FIELDWIDTH);
            fields_drawfield (playerfield(pnum), fields[pnum]);
            /* check moderator status */
            checkmoderatorstatus ();
        }
        break;
    case IN_KICK:
        {
            int pnum;
            char *token;
            token = strtok (data, " ");
            if (token == NULL) break;
            pnum = atoi (token);
            if (pnum >= MAX_PLAYERS || pnum < 0)
              break;
            if ((pnum == playernum) && !spectating)
                g_snprintf (buf, sizeof(buf),
                            _("%c%c*** You have been kicked from the game"),
                            TETRI_TB_C_DARK_GREEN, TETRI_TB_BOLD);
            else
                g_snprintf (buf, sizeof(buf),
                            _("%c*** %c%s%c%c has been kicked from the game"),
                            TETRI_TB_C_DARK_GREEN, TETRI_TB_BOLD,
                            playernames[pnum],
                            TETRI_TB_RESET, TETRI_TB_C_DARK_GREEN);
            partyline_text (buf);
            /*
             mark it so that leave message is not displayed when playerleave
             message is received (see above)
             */
            playernames[pnum][0] = 0;
            playerplaying[pnum] = 0;
        }
        break;
    case IN_TEAM:
        {
            int pnum;
            char *token;
            /* parse string */
            token = strtok (data, " ");
            if (token == NULL) break;
            pnum = atoi (token);
            if (pnum >= MAX_PLAYERS || pnum < 0)
              break;
            token = strtok (NULL, "");
            if (token == NULL) token = "";
            GTET_O_STRCPY (teamnames[pnum], token);
            playerlistupdate ();
            /* update fields display */
            fieldslabelupdate ();
            /* display */
            partylineupdate_team (playernames[pnum], teamnames[pnum]);
        }
        break;
    case IN_PLINE:
        {
            int pnum;
            char *token;
            token = strtok (data, " ");
            if (token == NULL) break;
            pnum = atoi (token);
            if (pnum >= MAX_PLAYERS || pnum < 0)
              break;
            token = strtok (NULL, "");
            if (token == NULL) token = "";
            //printf ("* %s\n", token);
            if (pnum == 0) {
                if (strncmp (token, "\04\04\04\04\04\04\04\04", 8) == 0) {
                    /* tetrix identification string */
                    tetrix = TRUE;
                }
                else
                {
                  gchar *line = nocolor (token);
                  gchar *aux;
                      
                  if (list_enabled && (list_issued > 0))
                  {
                    if (*line == '(')
                    {
                      partyline_add_channel (line);
                      break;
                    }
                      
                    if (!strncmp ("List", line, 4))
                    {
                      partyline_more_channel_lines ();
                      break;
                    }
                      
                    if (!strncmp ("TetriNET", line, 8))
                      break;
                      
                    if (!strncmp ("You do NOT", line, 10))
                    {
                      /* we will use the error message as list stopper */
                      list_issued--;
                      if (list_issued <= 0)
                        stop_list();
                      break;
                    }
                    
                    if (!strncmp ("Use", line, 3))
                      break;
                      
                    //if (line != NULL) g_free (line);
                  }
                  /* detect whenever we have joined a channel */
                  if (!strncmp ("has joined", &line[strlen (playernames[playernum])+1], 10))
                  {
                    partyline_joining_channel (&line[strlen (playernames[playernum])+20]);
                  }
                  else if (!strncmp ("Joined existing Channel", line, 23))
                  {
                    partyline_joining_channel (&line[26]);
                  }
                  else if (!strncmp ("Created new Channel", line, 19))
                  {
                    partyline_joining_channel (&line[22]);
                  }
                  else if (!strncmp ("You have joined", line, 15))
                  {
                    partyline_joining_channel (&line[16]);
                  }
                  
                  aux = g_strconcat ("You tell ", playernames[playernum], ": --- MARK ---", NULL);
                  if (!strcmp (aux, line))
                  {
                    list_issued--;
                    if (list_issued <= 0)
                      stop_list ();
                    break;
                  }
                  aux = g_strconcat ("{", playernames[playernum], "} --- MARK ---", NULL);
                  if (!strcmp (aux, line))
                  {
                    list_issued--;
                    if (list_issued <= 0)
                      stop_list ();
                    break;
                  }
                    
                  if (tetrix) {
                    g_snprintf (buf, sizeof(buf), "*** %s", token);
                    partyline_text (buf);
                    break;
                  }
                  else
                    plinemsg ("Server", token);
                }
            }
            else
            {
              if (pnum == playernum)
              {
                gchar *line = nocolor (token);
                if (!strncmp (line, "(msg) --- MARK ---", 18))
                {
                  list_issued--;
                  if (list_issued <= 0)
                    stop_list();
                }
                else
                  plinemsg (playernames[pnum], token);
                //g_free (line);
              }
              else
                plinemsg (playernames[pnum], token);
            }
        }
        break;
    case IN_PLINEACT:
        {
            int pnum;
            char *token;
            token = strtok (data, " ");
            if (token == NULL) break;
            pnum = atoi (token);
            if (pnum >= MAX_PLAYERS || pnum < 0)
              break;
            token = strtok (NULL, "");
            if (token == NULL) token = "";
            /* display it */
            plineact (playernames[pnum], token);
        }
        break;
    case IN_PLAYERLOST:
        {
            int pnum;
            pnum = atoi (data);
            if (pnum >= MAX_PLAYERS || pnum < 0)
              break;
            /* player is out */
            playerplaying[pnum] = 0;
        }
        break;
    case IN_PLAYERWON:
        {
            int pnum;
            pnum = atoi (data);
            if (pnum >= MAX_PLAYERS || pnum < 0)
              break;
            if (teamnames[pnum][0])
                g_snprintf (buf, sizeof(buf),
                            _("%c*** Team %c%s%c%c has won the game"),
                            TETRI_TB_C_DARK_RED, TETRI_TB_BOLD,
                            teamnames[pnum],
                            TETRI_TB_RESET, TETRI_TB_C_DARK_RED);
            else
                g_snprintf (buf, sizeof(buf),
                            _("%c*** %c%s%c%c has won the game"),
                            TETRI_TB_C_DARK_RED, TETRI_TB_BOLD,
                            playernames[pnum],
                            TETRI_TB_RESET, TETRI_TB_C_DARK_RED);
            partyline_text (buf);
        }
        break;
    case IN_BTRIXNEWGAME:
        btrixgame = 1;
        // Leak through to NEWGAME.
    case IN_NEWGAME:
        {
            int i, j;
            char bfreq[101], sfreq[101];
            sscanf (data, "%d %d %d %d %d %d %d %100s %100s %d %d",
                    &initialstackheight, &initiallevel,
                    &linesperlevel, &levelinc, &speciallines,
                    &specialcount, &specialcapacity,
                    bfreq, sfreq, &levelaverage, &classicmode);

            bfreq[100] = 0;
            sfreq[100] = 0;
            
            /* initialstackheight == seems ok */
            /* initiallevel == seems ok */
            /* linesperlevel == seems ok */
            /* levelinc == seems ok */
            
            if (!speciallines) /* does divide by this number */
              speciallines = 1;
            
            /* specialcount == seems ok */
            
            if ((unsigned int)specialcapacity > sizeof(specialblocks))
              specialcapacity = sizeof(specialblocks);
            
            /* levelaverage == seems ok */
            /* classicmode == seems ok */
            
            /*
             decoding the 11233345666677777 block frequecy thingies:
             */
            for (i = 0; i < 7; i ++) blockfreq[i] = 0;
            for (i = 0; i < 9; i ++) specialfreq[i] = 0;
            /* count frequencies */
            for (i = 0; bfreq[i]; i ++)
                blockfreq[bfreq[i]-'1'] ++;
            for (i = 0; sfreq[i]; i ++)
                specialfreq[sfreq[i]-'1'] ++;
            /* make it cumulative */
            for (i = 0, j = 0; i < 7; i ++) {
                j += blockfreq[i];
                blockfreq[i] = j;
            }
            for (i = 0, j = 0; i < 9; i ++) {
                j += specialfreq[i];
                specialfreq[i] = j;
            }
            /* sanity checks */
            if (blockfreq[6] < 100) blockfreq[6] = 100;
            if (specialfreq[8] < 100) specialfreq[8] = 100;
            tetrinet_startgame ();
            commands_checkstate ();
            partyline_fmt (_("%c*** The game has %cstarted"),
                           TETRI_TB_C_BRIGHT_RED, TETRI_TB_BOLD);
            show_stop_button ();
            /* switch to playerfields when game starts */
            show_fields_page ();
        }
        break;
    case IN_INGAME:
        {
            FIELD field;
            int x, y, i;
            ingame = TRUE;
            playing = paused = FALSE;
            if (!spectating) {
                for (y = 0; y < FIELDHEIGHT; y ++)
                    for (x = 0; x < FIELDWIDTH; x ++)
                        field[y][x] = randomnum(5) + 1;
                tetrinet_updatefield (field);
                tetrinet_sendfield (1);
                fields_drawfield (playerfield(playernum), fields[playernum]);
            }
            for (i = 0; i <= 6; i ++) playerlevels[i] = -1;
            tetrinet_setspeciallabel (-1);
            fields_gmsginput (FALSE);
            fields_gmsginputclear ();
            commands_checkstate ();
            partyline_fmt(_("%c*** The game is %cin progress"),
                          TETRI_TB_C_BRIGHT_RED, TETRI_TB_BOLD);;
            show_stop_button ();
        }
        break;
    case IN_PAUSE:
	{
	    int newstate = atoi(data);
            /* bail out if no state change */
            if (! (newstate ^ paused)) break;
	    if (newstate) {
		tetrinet_pausegame ();
		partyline_fmt (_("%c*** The game has %cpaused"),
                               TETRI_TB_C_BRIGHT_RED, TETRI_TB_BOLD);
                fields_attdeffmt (_("The game has %c%cpaused"),
                                  TETRI_TB_BOLD, TETRI_TB_C_DARK_GREEN);
	    }
	    else {
		tetrinet_resumegame ();
		partyline_fmt (_("%c*** The game has %cresumed"),
                               TETRI_TB_C_BRIGHT_RED, TETRI_TB_BOLD);
                fields_attdeffmt (_("The game has %c%cresumed"),
                                  TETRI_TB_BOLD, TETRI_TB_C_DARK_GREEN);
	    }
	    commands_checkstate ();
	    break;
	}
    case IN_ENDGAME:
        tetrinet_endgame ();
        commands_checkstate ();
        partyline_fmt (_("%c*** The game has %cended"),
                       TETRI_TB_C_BRIGHT_RED, TETRI_TB_BOLD);
        show_start_button ();
        /* go back to partyline when game ends */
        show_partyline_page ();

        // Return delay to normal.
        btrixgame = 0;
        break;
    case IN_F:
        {
            int pnum;
            char *p, *s;
            s = strtok (data, " ");
            if (s == NULL) break;
            pnum = atoi (s);
            if (pnum >= MAX_PLAYERS || pnum < 0)
              break;
            s = strtok (NULL, "");
            if (s == NULL) break;
            if (*s >= '0') {
                /* setting entire field */
                p = (char *)fields[pnum];
                for (; *s; s ++, p ++)
                    *p = translateblock (*s);
            }
            else {
                /* setting specific locations */
                int block = 0, x, y;
                for(; *s; s++) {
                    if (*s < '0' && *s >= '!') block = *s - '!';
                    else { /* welcome to ASCII hell, x and y tested though */
                        x = *s - '3';
                        y = *(++s) - '3';
                        if (x >= 0 && x < FIELDWIDTH && y >= 0 && y < FIELDHEIGHT)
                            fields[pnum][y][x] = block;
                    }
                }
            }
            if (ingame) fields_drawfield (playerfield(pnum), fields[pnum]);
        }
        break;
    case IN_SB:
        /* special block */
        {
            int sbnum, to, from;
            char *token, *sbid;
            token = strtok (data, " ");
            if (token == NULL) break;
            to = atoi (token);
            if (to >= MAX_PLAYERS || to < 0)
              break;
            sbid = strtok (NULL, " ");
            if (sbid == NULL) break;
            token = strtok (NULL, "");
            if (token == NULL) break;
            from = atoi(token);
            if (from >= MAX_PLAYERS || from < 0)
              break;
            for (sbnum = 0; sbinfo[sbnum].id; sbnum ++)
                if (strcmp (sbid, sbinfo[sbnum].id) == 0) break;
            if (!sbinfo[sbnum].id) break;
            if (ingame) tetrinet_dospecial (from, to, sbnum);
        }
        break;
    case IN_LVL:
        {
            char *token;
            int pnum;
            token = strtok (data, " ");
            if (token == NULL) break;
            pnum = atoi (token);
            if (pnum >= MAX_PLAYERS || pnum < 0)
              break;
            token = strtok (NULL, "");
            if (token == NULL) break;
            playerlevels[pnum] = atoi (token);
            if (!pnum && !playerlevels[pnum] && !pnumrec) {
                client_outmessage(OUT_CLIENTINFO, APPNAME" "APPVERSION);
                pnumrec = 1;
            }
            if (ingame) tetrinet_updatelevels ();
        }
        break;
    case IN_GMSG:
        fields_gmsgadd (data);
        sound_playsound (S_MESSAGE);
        break;
    case IN_WINLIST:
        {
            char *token, *token2;
            int team, score;
            winlist_clear ();
            token = strtok (data, " ");
            if (token == NULL) break;
            do {
                switch (*token) {
                case 'p': team = FALSE; break;
                case 't': team = TRUE; break;
                default: team = FALSE; break;
                }
                token ++;
                token2 = token;
                while (*token2 != ';' && *token2 != 0) token2 ++;
                *token2 = 0;
                token2 ++;
                score = atoi (token2);
                winlist_additem (team, token, score);
            } while ((token = strtok (NULL, " ")) != NULL);
        }
        break;
    case IN_SPECLIST:
        {
            char *token, buf[1024];
            speclist_clear ();
            token = strtok (data, " ");
            if (token == NULL) break;
            g_snprintf (buf, sizeof(buf),
                        _("%c*** You have joined %c%s%c%c"),
                        TETRI_TB_C_DARK_BLUE, TETRI_TB_BOLD, token,
			TETRI_TB_RESET, TETRI_TB_C_DARK_BLUE);
            partyline_text (buf);
            while ((token = strtok (NULL, " ")) != NULL) speclist_add (token);
            playerlistupdate ();
        }
        break;
    case IN_SPECJOIN:
        {
            char *name, *info, buf[1024];
            name = strtok (data, " ");
            if (name == NULL) break;
            speclist_add (name);
            info = strtok (NULL, "");
            if (info == NULL) info = "";
            g_snprintf (buf, sizeof(buf),
                        _("%c*** %c%s%c%c has joined the spectators"
                        " %c%c(%c%s%c%c%c)"),
                        TETRI_TB_C_DARK_BLUE, TETRI_TB_BOLD,
                        name,
                        TETRI_TB_RESET, TETRI_TB_C_DARK_BLUE,
                        TETRI_TB_C_GREY, TETRI_TB_BOLD,  TETRI_TB_BOLD,
                        info,
                        TETRI_TB_RESET, TETRI_TB_C_GREY, TETRI_TB_BOLD);
            partyline_text (buf);
            playerlistupdate ();
        }
        break;
    case IN_SPECLEAVE:
        {
            char *name, *info, buf[1024];
            name = strtok (data, " ");
            if (name == NULL) break;
            speclist_remove (name);
            info = strtok (NULL, "");
            if (info == NULL) info = "";
            g_snprintf (buf, sizeof(buf),
                        _("%c*** %c%s%c%c has left the spectators"
                        " %c%c(%c%s%c%c%c)"),
                        TETRI_TB_C_DARK_BLUE, TETRI_TB_BOLD,
                        name,
                        TETRI_TB_RESET, TETRI_TB_C_DARK_BLUE,
                        TETRI_TB_C_GREY, TETRI_TB_BOLD,  TETRI_TB_BOLD,
                        info,
                        TETRI_TB_RESET, TETRI_TB_C_GREY, TETRI_TB_BOLD);
            partyline_text (buf);
            playerlistupdate ();
        }
        break;
    case IN_SMSG:
        {
            char *name, *text;
            name = strtok (data, " ");
            if (name == NULL) break;
            text = strtok (NULL, "");
            if (text == NULL) text = "";
            plinesmsg (name, text);
        }
        break;
    case IN_SACT:
        {
            char *name, *text;
            name = strtok (data, " ");
            if (name == NULL) break;
            text = strtok (NULL, "");
            if (text == NULL) text = "";
            plinesact (name, text);
        }
        break;
    default:
      break;
    }
}

void tetrinet_playerline (const char *text)
{
  char buf[1024];
  const char *p;

    if (text[0] == '/') {
        p = text+1;
        if (strncasecmp (p, "me ", 3) == 0) {
            p += 3;
            while (*p && isspace(*p)) p++;
            g_snprintf (buf, sizeof(buf), "%d %s", playernum, p);
            client_outmessage (OUT_PLINEACT, buf);
            if (spectating)
                plinesact (nick, p);
            else
                plineact (nick, p);
            return;
        }
        if (tetrix) {
            g_snprintf (buf, sizeof(buf), "%d %s", playernum, text);
            client_outmessage (OUT_PLINE, buf);
            return;
        }
        /* send the message without showing it in the partyline */
        g_snprintf (buf, sizeof(buf), "%d %s", playernum, text);
        client_outmessage (OUT_PLINE, buf);
        return;
    }
    g_snprintf (buf, sizeof(buf), "%d %s", playernum, text);
    client_outmessage (OUT_PLINE, buf);
    if (spectating)
        plinesmsg (nick, text);
    else
        plinemsg (nick, text);
}

void tetrinet_changeteam (const char *newteam)
{
    char buf[128];

    GTET_O_STRCPY (team, newteam);

    if (connected) {
        g_snprintf (buf, sizeof(buf), "%d %s", playernum, team);
        client_outmessage (OUT_TEAM, buf);
        tetrinet_inmessage (IN_TEAM, buf);
        partyline_namelabel (nick, team);
    }
}

#if 0
#define TETRINET_CHECK_COMPAT_SENDFIELD(x) /* do nothing */
#else
static void tetrinet_old_sendfield (int reset, const char *new_buf)
{
    int x, y, i, d = FALSE;
    char buf[1024], buf2[1024], *p;

    if (reset) goto sendwholefield;

    g_snprintf (buf, sizeof(buf), "%d ", playernum);
    /* find differences between the fields */
    for (i = 0; i < 15; i ++) {
        p = buf2 + 1;
        buf2[0] = '!' + i;
        for (y = 0; y < FIELDHEIGHT; y ++)
            for (x = 0; x < FIELDWIDTH; x ++)
                if (sentfield[y][x] != fields[playernum][y][x]
                    && fields[playernum][y][x] == i)
                {
                    *p++ = x + '3';
                    *p++ = y + '3';
                }
        if (p > buf2+1) {
            *p = 0;
            GTET_O_STRCAT (buf, buf2);
            d = TRUE;
        }
    }
    if (!d) return; /* no differences */
    if (strlen (buf) >= FIELDHEIGHT*FIELDWIDTH+2) {
        /* sending entire field is more efficient */
    sendwholefield:
        p = buf2;
        for (y = 0; y < FIELDHEIGHT; y ++)
            for (x = 0; x < FIELDWIDTH; x ++)
                *p++ = blocks[(int)fields[playernum][y][x]];
        *p = 0;
        g_snprintf (buf, sizeof(buf), "%d %s", playernum, buf2);
    }
    if (strcmp(new_buf, buf))
      g_warning("sendfield ... non compat. data\n"
                "\tBEG: sendfield data\n"
                "\t old=%s\n"
                "\t new=%s\n"
                "\tEND: sendfield data\n",
                buf, new_buf);
#if 0
    /* send it */
    client_outmessage (OUT_F, buf);
    /* update the one in our memory */
    copyfield (sentfield, fields[playernum]);
#endif
}
#define TETRINET_CHECK_COMPAT_SENDFIELD(x) tetrinet_old_sendfield (reset, (x))
#endif

void tetrinet_sendfield (int reset)
{
  int x, y, i, d = 0; /* d is the number of differences */
  char buf[1024], *p;

  char diff_buf[15][(FIELDWIDTH + 1)* FIELDHEIGHT * 2] = {{0}};

  int row_count[15] = {1,1,1,1,1, 1,1,1,1,1, 1,1,1,1,1};
  
  g_snprintf (buf, sizeof(buf), "%d ", playernum);

  if(!reset) {
    /* Find out which blocks changed, how, and how many */
    for (y = 0; y < FIELDHEIGHT; y ++) {
      for (x = 0; x < FIELDWIDTH; x ++) {

	const int block = fields[playernum][y][x];

        if ((block < 0) || (block >= 15))
        {
          g_warning("sendfield shouldn't reach here, block=%d\n", block);
          continue;
        }
        
	if (block != sentfield[y][x]) {
	  diff_buf[block][row_count[block]++] = x + '3';
	  diff_buf[block][row_count[block]++] = y + '3';
	  d += 2;
	}
      }
    }
    if (d == 0) return; /* no differences */

    for (i = 0; i < 15; ++i)
      if (row_count[i] > 1)
        ++d; /* add an extra value for the '!'+i at the start */
  }
  
  if (reset || d >= (FIELDHEIGHT*FIELDWIDTH)) {
    /* sending entire field is more efficient */
    p = buf + 2;
    for (y = 0; y < FIELDHEIGHT; y ++)
      for (x = 0; x < FIELDWIDTH; x ++)
	*p++ = blocks[(int)fields[playernum][y][x]];
    *p = 0;
  }
  else { /* so now we need to create the buffer strings */
    for(i = 0; i < 15; ++i) {
      if(row_count[i] > 1) {
	diff_buf[i][0] = '!' + i;
	GTET_O_STRCAT (buf, diff_buf[i]);
      }
    }
  }

  TETRINET_CHECK_COMPAT_SENDFIELD(buf);
  
  /* send it */
  client_outmessage (OUT_F, buf);
  /* update the one in our memory */
  copyfield (sentfield, fields[playernum]);
}

void tetrinet_updatefield (FIELD field)
{
    copyfield (fields[playernum], field);
}

void tetrinet_resendfield (void)
{
    char buf[1024], buf2[1024], *p;
    int x, y;
    p = buf2;
    for (y = 0; y < FIELDHEIGHT; y ++)
        for (x = 0; x < FIELDWIDTH; x ++)
            *p++ = blocks[(int)sentfield[y][x]];
    *p = 0;
    g_snprintf (buf, sizeof(buf), "%d %s", playernum, buf2);
    client_outmessage (OUT_F, buf);
}

void tetrinet_redrawfields (void)
{
    int i;

    fieldslabelupdate ();
    if (ingame) for (i = 1; i <= 6; i ++)
        fields_drawfield (playerfield(i), fields[i]);
}

static void tetrinet_setspeciallabel (int sb)
{
    int sbnum;
    if (sb == -1) {
        fields_setspeciallabel (_("No special blocks"));
    }
    else {
        for (sbnum = 0; sbinfo[sbnum].id; sbnum ++)
            if (sbinfo[sbnum].block == sb) break;
        fields_setspeciallabel (sbinfo[sbnum].info);
    }

}

/* adds a special block to player's collection */
static void tetrinet_addspecial (char sb)
{
    int l, i;
    if (specialblocknum >= specialcapacity) return; /* too many ! */

    /* add to a random location between, 0 and last offset.
     * Ie. If you have X sps already it gets added between 0 and X */
    l = randomnum(++specialblocknum);
    for (i = specialblocknum; i > l; i --)
        specialblocks[i] = specialblocks[i-1];
    specialblocks[l] = sb;
    tetrinet_setspeciallabel (specialblocks[0]);
}

/* adds a random special block to the field */
static void tetrinet_addsbtofield (int count)
{
    int s, n, c, x, y, i, j;
    char sb;
    FIELD field;
    copyfield (field, fields[playernum]);
    for (i = 0; i < count; i ++) {
        /* get a random special block */
        s = 0;
        n = randomnum(100);
        while (n >= specialfreq[s]) s ++;
        sb = 6 + s;
        /* sb is the special block that we want */
        /* count the number of non-special blocks on the field */
        c = 0;
        for (y = 0; y < FIELDHEIGHT; y ++)
            for (x = 0; x < FIELDWIDTH; x ++)
                if (field[y][x] > 0 && field[y][x] < 6) c ++;
        if (c == 0) { /* drop block */
            /* i *think* this is how it works in the original -
             blocks are not dropped on existing specials,
             and it tries again to find another spot...
             this is because... when a large number of
             blocks are dropped, usually all columns get 1 block
             but sometimes a column or two doesnt get a block */
            for (j = 0; j < 20; j ++) {
                n = randomnum (FIELDWIDTH);
                for (y = 0; y < FIELDHEIGHT; y ++)
                    if (field[y][n]) break;
                if (y == FIELDHEIGHT || field[y][n] < 6) break;
            }
            if (j == 20) goto end;
            y --;
            field[y][n] = sb;
        }
        else { /* choose a random location */
            n = randomnum (c);
            for (y = 0; y < FIELDHEIGHT; y ++)
                for (x = 0; x < FIELDWIDTH; x ++)
                    if (field[y][x] > 0 && field[y][x] < 6) {
                        if (n == 0) {
                            field[y][x] = sb;
                            goto next;
                        }
                        n --;
                    }
        next:
            /* fall through */ ;
        }
    }
end:
    tetrinet_updatefield (field);
}

/* specials */
void tetrinet_dospecial (int from, int to, int type)
{
    FIELD field;
    int x, y, i;
    char buf[512], buf2[256];

    switch (type)
    {
      case S_ADDALL1:
      case S_ADDALL2:
      case S_ADDALL4: /* bad for everyone ... */
        g_assert(!to);
        if (from == playernum)
          g_snprintf (buf, sizeof(buf), "%c%c%s%c",
                      TETRI_TB_BOLD,
                      TETRI_TB_C_BLACK,
                      sbinfo[type].info,
                      TETRI_TB_RESET);
        else
          g_snprintf (buf, sizeof(buf), "%c%c%s%c%c",
                      TETRI_TB_BOLD,
                      TETRI_TB_C_BRIGHT_RED,
                      sbinfo[type].info,
                      TETRI_TB_C_BRIGHT_RED,
                      TETRI_TB_BOLD);
        break;
        
      case S_ADDLINE:
      case S_CLEARBLOCKS:
      case S_CLEARSPECIAL:
      case S_BLOCKQUAKE:
      case S_BLOCKBOMB: /* badish stuff for someone */
        if (to == playernum)
          g_snprintf (buf, sizeof(buf), "%c%c%s%c",
                      TETRI_TB_BOLD,
                      TETRI_TB_C_BRIGHT_RED,
                      sbinfo[type].info,
                      TETRI_TB_RESET);
        else if (from == playernum)
          g_snprintf (buf, sizeof(buf), "%c%c%s%c",
                      TETRI_TB_BOLD,
                      TETRI_TB_C_BLACK,
                      sbinfo[type].info,
                      TETRI_TB_RESET);
        else
          g_snprintf (buf, sizeof(buf), "%c%c%s%c%c",
                      TETRI_TB_BOLD,
                      TETRI_TB_C_DARK_RED,
                      sbinfo[type].info,
                      TETRI_TB_C_DARK_RED,
                      TETRI_TB_BOLD);
        break;

      case S_CLEARLINE:
      case S_NUKEFIELD:
      case S_SWITCH:
      case S_GRAVITY: /* goodish stuff for someone */
        if (to == playernum)
          g_snprintf (buf, sizeof(buf), "%c%c%s%c",
                      TETRI_TB_BOLD,
                      TETRI_TB_C_BRIGHT_GREEN,
                      sbinfo[type].info,
                      TETRI_TB_RESET);
        else
          g_snprintf (buf, sizeof(buf), "%c%c%s%c%c",
                      TETRI_TB_BOLD,
                      TETRI_TB_C_DARK_GREEN,
                      sbinfo[type].info,
                      TETRI_TB_C_DARK_GREEN,
                      TETRI_TB_BOLD);
        break;
        
      default:
        g_assert_not_reached();
    }
      
    if (to) {
      if (to == playernum)
        g_snprintf (buf2, sizeof(buf2), _(" on %c%c%s%c%c"),
                    TETRI_TB_BOLD,
                    TETRI_TB_C_BRIGHT_BLUE,
                    playernames[to],
                    TETRI_TB_C_BRIGHT_BLUE,
                    TETRI_TB_BOLD);
      else
        g_snprintf (buf2, sizeof(buf2), _(" on %c%c%s%c%c"),
                    TETRI_TB_BOLD,
                    TETRI_TB_C_DARK_BLUE,
                    playernames[to],
                    TETRI_TB_C_DARK_BLUE,
                    TETRI_TB_BOLD);
        GTET_O_STRCAT (buf, buf2);
    }
    else
    {
      g_snprintf (buf2, sizeof(buf2), _(" to All"));
      GTET_O_STRCAT (buf, buf2);
    }
    
    if (from) {
      if (from == playernum)
        g_snprintf (buf2, sizeof(buf2), _(" by %c%c%s%c%c"),
                    TETRI_TB_BOLD,
                    TETRI_TB_C_BRIGHT_BLUE,
                    playernames[from],
                    TETRI_TB_C_BRIGHT_BLUE,
                    TETRI_TB_BOLD);
      else
        g_snprintf (buf2, sizeof(buf2), _(" by %c%c%s%c%c"),
                    TETRI_TB_BOLD,
                    TETRI_TB_C_DARK_BLUE,
                    playernames[from],
                    TETRI_TB_C_DARK_BLUE,
                    TETRI_TB_BOLD);
        GTET_O_STRCAT (buf, buf2);
    }
    fields_attdefmsg (buf);

    if (!playing) return; /* we're not playing !!! */

    if (from == playernum && type == S_SWITCH) {
        /* we have to switch too... */
        from = to;
        to = playernum;
    }

    if (!(to == 0 && from != playernum) && to != playernum)
        return; /* not for this player */

    if (to == 0)
        /* same team */
        if (team[0] && strcmp (teamnames[from], team) == 0) return;

    copyfield (field, fields[playernum]);

    switch (type)
    {
        /* these add alls need to determine team... */
    case S_ADDALL1:
        tetris_addlines (1, 2);
        break;
    case S_ADDALL2:
        tetris_addlines (2, 2);
        break;
    case S_ADDALL4:
        tetris_addlines (4, 2);
        break;
    case S_ADDLINE:
        tetris_addlines (1, 1);
        break;
    case S_CLEARLINE:
        for (y = FIELDHEIGHT-1; y > 0; y --)
            for (x = 0; x < FIELDWIDTH; x ++)
                field[y][x] = field[y-1][x];
        for (x = 0; x < FIELDWIDTH; x ++) field[0][x] = 0;
        tetrinet_updatefield (field);
        break;
    case S_NUKEFIELD:
        memset (field, 0, FIELDWIDTH*FIELDHEIGHT);
        tetrinet_updatefield (field);
        break;
    case S_CLEARBLOCKS:
        for (i = 0; i < 10; i ++)
            field[randomnum(FIELDHEIGHT)][randomnum(FIELDWIDTH)] = 0;
        tetrinet_updatefield (field);
        break;
    case S_SWITCH:
        copyfield (field, fields[from]);
        for (y = 0; y < 6; y ++)
            for (x = 0; x < FIELDWIDTH; x ++)
                if (field[y][x]) goto bleep;
    bleep:
        i = 6 - y;
        if (i) {
            /* need to move field down i lines */
            for (y = FIELDHEIGHT-1; y >= i; y --)
                for (x = 0; x < FIELDWIDTH; x ++)
                    field[y][x] = field[y-i][x];
            for (y = 0; y < i; y ++)
                for (x = 0; x < FIELDWIDTH; x ++) field[y][x] = 0;
        }
        tetrinet_updatefield (field);
        break;
    case S_CLEARSPECIAL:
        for (y = 0; y < FIELDHEIGHT; y ++)
            for (x = 0; x < FIELDWIDTH; x ++)
                if (field[y][x] > 5) field[y][x] = randomnum (5) + 1;
        tetrinet_updatefield (field);
        break;
    case S_GRAVITY:
        for (y = 0; y < FIELDHEIGHT; y ++)
            for (x = 0; x < FIELDWIDTH; x ++)
                if (field[y][x] == 0) {
                    /* move the above blocks down */
                    for (i = y; i > 0; i --)
                        field[i][x] = field[i-1][x];
                    field[0][x] = 0;
                }
        tetrinet_updatefield (field);
        break;
    case S_BLOCKQUAKE:
        for (y = 0; y < FIELDHEIGHT; y ++) {
            /* [ the original approximation of blockquake frequencies were
                 not quite correct ] */
            /* ### This is a much better approximation and probably how the */
            /* ### original tetrinet does it */
            int s = 0;
            i = randomnum (22);
            if (i < 1) s ++;
            if (i < 4) s ++;
            if (i < 11) s ++;
            if (randomnum(2)) s = -s;
            tetrinet_shiftline (y, s, field);
            /* ### Corrected by Pihvi */
        }
        tetrinet_updatefield (field);
        break;
    case S_BLOCKBOMB:
        {
            int ax[] = {-1, 0, 1, 1, 1, 0, -1, -1};
            int ay[] = {-1, -1, -1, 0, 1, 1, 1, 0};
            int c = 0;
            char block;
            /* find all bomb blocks */
            for (y = 0; y < FIELDHEIGHT; y ++)
                for (x = 0; x < FIELDWIDTH; x ++)
                    if (field[y][x] == 14) {
                        /* remove the bomb */
                        field[y][x] = 0;
                        /* grab the squares around it */
                        for (i = 0; i < 8; i ++) {
                            if (y+ay[i] >= FIELDHEIGHT || y+ay[i] < 0 ||
                                x+ax[i] >= FIELDWIDTH || x+ax[i] < 0) continue;
                            block = field[y+ay[i]][x+ax[i]];
                            if (block == 14) block = 0;
                            else field[y+ay[i]][x+ax[i]] = 0;
                            buf[c] = block;
                            c ++;
                        }
                    }
            /* scatter blocks */
            for (i = 0; i < c; i ++)
                field[randomnum(FIELDHEIGHT-6)+6][randomnum(FIELDWIDTH)] = buf[i];
        }
        tetrinet_updatefield (field);
        break;
    }
    tetris_removelines (NULL);
    tetrinet_sendfield (0);
    tetris_drawcurrentblock ();
}

static void tetrinet_shiftline (int l, int d, FIELD field)
{
    int i;
    if (d > 0) { /* to the right */
        for (i = FIELDWIDTH-1; i >= d; i --)
            field[l][i] = field[l][i-d];
        for (; i >= 0; i --) field[l][i] = 0;
    }
    if (d < 0) { /* to the left */
        for (i = 0; i < FIELDWIDTH+d; i ++)
            field[l][i] = field[l][i-d];
        for (; i < FIELDWIDTH; i ++) field[l][i] = 0;
    }
    /* if d == 0 do nothing */
}

/* returns a random block */
static int tetrinet_getrandomblock (void)
{
    int i = 0, n = randomnum (100);
    while (n >= blockfreq[i]) i ++;
    return i;
}

static int tetrinet_timeoutduration (void)
{
    return level<=100 ? 1005-level*10 : 5;
}


/*****************/
/* tetrisy stuff */
/*****************/
static void tetrinet_settimeout (int duration);
static gint tetrinet_timeout (void);
static void tetrinet_solidify (void);
static void tetrinet_nextblock (void);
static gint tetrinet_nextblocktimeout (void);
static int tetrinet_removelines (void);
static gint tetrinet_removelinestimeout (void);

TETRISBLOCK blankblock =
{ {0,0,0,0}, {0,0,0,0}, {0,0,0,0}, {0,0,0,0} };

gint movedowntimeout = 0, nextblocktimeout = 0;
int downcount = 0, downpressed = 0;
int nextblock, nextorient;

void tetrinet_startgame (void)
{
    int i;
    linecount = slines = llines = 0;
    level = initiallevel;
    for (i = 0; i <= 6; i ++) playerlevels[i] = initiallevel;
    for (i = 0; i <= 6; i ++)
        if (playernames[i][0]) playerplaying[i] = TRUE;
    tetrinet_updatelevels ();
    fields_setlines (0);
    tetrinet_setspeciallabel (-1);
    fields_gmsginput (FALSE);
    fields_gmsginputclear ();
    fields_attdefclear ();
    fields_gmsgclear ();
    paused = FALSE;
    specialblocknum = 0;
    ingame = TRUE;
    clearallfields ();
    if (!spectating) {
        playing = TRUE;
        nextblock = tetrinet_getrandomblock ();
        nextorient = tetris_randomorient (nextblock);
        fields_drawnextblock (tetris_getblock(nextblock, nextorient));
        tetris_addlines (initialstackheight, 1);
        tetrinet_sendfield (1);
        tetrinet_nextblock ();
    }
    sound_playsound (S_GAMESTART);
}

void tetrinet_pausegame (void)
{
    paused = TRUE;
}

void tetrinet_resumegame (void)
{
    paused = FALSE;
}

void tetrinet_playerlost (void)
{
    int x, y;
    char buf[11];
    FIELD field;
    playing = FALSE;
    /* fix up the display */
    for (y = 0; y < FIELDHEIGHT; y ++)
        for (x = 0; x < FIELDWIDTH; x ++)
            field[y][x] = randomnum(5) + 1;
    tetrinet_updatefield (field);
    fields_drawfield (playerfield(playernum), fields[playernum]);
    fields_drawnextblock (blankblock);
    /* send field */
    tetrinet_sendfield (1);
    /* post message */
    g_snprintf (buf, sizeof(buf), "%d", playernum);
    client_outmessage (OUT_PLAYERLOST, buf);
    /* make a sound */
    sound_playsound (S_YOULOSE);
    /* end timeout thingies */
    if (movedowntimeout)
        sched_timer_remove (movedowntimeout);
    if (nextblocktimeout)
        sched_timer_remove (nextblocktimeout);
    movedowntimeout = nextblocktimeout = 0;
    tetris_makeblock (-1, 0);
}

void tetrinet_endgame (void)
{
    int i, c = 0;
    for (i = 1; i <= 6; i ++)
        if (playerplaying[i]) c ++;
    if (playing && playercount > 1 && c == 1)
        sound_playsound (S_YOUWIN);
    ingame = playing = FALSE;
    if (movedowntimeout)
        sched_timer_remove (movedowntimeout);
    if (nextblocktimeout)
        sched_timer_remove (nextblocktimeout);
    movedowntimeout = nextblocktimeout = 0;
    tetris_makeblock (-1, 0);
    fields_drawnextblock (blankblock);
    clearallfields ();
    /* don't clear messages when game ends */
    /*
    fields_attdefclear ();
    fields_gmsgclear ();
    */
    specialblocknum = 0;
    fields_drawspecials ();
    fields_setlines (-1);
    fields_setlevel (-1);
    fields_setactivelevel (-1);
    fields_setspeciallabel (NULL);
    if (gmsgstate)
    {
      gmsgstate = 0;
      fields_gmsginput (FALSE);
      fields_gmsginputclear ();
      unblock_keyboard_signal ();
    }
}

void tetrinet_updatelevels (void)
{
    int c = 0, t = 0, i;
    if (levelaverage) {
        /* average levels */
        for (i = 1; i <= 6; i ++) {
            if (playerplaying[i]) {
                c ++;
                t += playerlevels[i];
            }
        }
        if(c) level = t / c;
        fields_setactivelevel (level);
    }
    fields_setlevel (playerlevels[bigfieldnum]);
}

void tetrinet_settimeout (int duration)
{
    if (movedowntimeout)
        sched_timer_remove (movedowntimeout);
    movedowntimeout = sched_timeout_add (duration, (sched_timer_func) tetrinet_timeout,
                                     NULL);
}

void tetrinet_removetimeout (void)
{
    if (movedowntimeout)
        sched_timer_remove (movedowntimeout);
    movedowntimeout = 0;
}

gint tetrinet_timeout (void)
{
    if (paused) return TRUE;
    if (!playing) return FALSE;
    if (downpressed) downpressed ++;
    if (tetris_blockdown ()) {
        if (!playing) /* player died within tetris_blockdown() */
            return FALSE;
        if (downcount) {
            tetrinet_solidify ();
            downcount = 0;
        }
        else downcount ++;
    }
    else downcount = 0;
    tetris_drawcurrentblock ();
    return TRUE;
}

void tetrinet_solidify (void)
{
    int sound;
    tetris_solidify ();
    tetrinet_nextblock ();
    sound = tetrinet_removelines ();
    if (sound>=0) sound_playsound (sound);
    else sound_playsound (S_SOLIDIFY);
    tetrinet_sendfield (0);
}

void tetrinet_nextblock (void)
{
    if (nextblocktimeout) return;
    tetrinet_removetimeout ();
    nextblocktimeout =
        sched_timeout_add ((btrixgame ? 0 : NEXTBLOCKDELAY),
                       (sched_timer_func) tetrinet_nextblocktimeout, NULL);
}

gint tetrinet_nextblocktimeout (void)
{
    if (paused) return TRUE; /* come back later */
    if (!playing) return FALSE;
    if (tetris_makeblock (nextblock, nextorient)) {
        /* player died */
        return FALSE;
    }
    nextblock = tetrinet_getrandomblock ();
    nextorient = tetris_randomorient (nextblock);
    fields_drawnextblock (tetris_getblock(nextblock, nextorient));
    tetris_drawcurrentblock ();
    nextblocktimeout = 0;
    tetrinet_settimeout (tetrinet_timeoutduration());
    return FALSE;
}

int tetrinet_removelines ()
{
    char buf[256];
    int lines, i, j, sound = -1, slcount;
    lines = tetris_removelines (buf);
    if (lines) {
        linecount += lines;
        slines += lines;
        llines += lines;
        fields_setlines (linecount);
        /* save specials */
        for (i = 0; i < lines; i ++)
            for (j = 0; buf[j]; j ++)
                tetrinet_addspecial (buf[j]);
        fields_drawspecials ();
        /* add specials to field */
        slcount = slines / speciallines;
        slines %= speciallines;
        tetrinet_addsbtofield (specialcount * slcount);
        /* work out what noise to make */
        if (lines == 4) sound = S_TETRIS;
        else if (buf[0]) sound = S_SPECIALLINE;
        else sound = S_LINECLEAR;
        /* increment level */
        if (llines >= linesperlevel) {
            char buf[32];
            while (llines >= linesperlevel) {
                playerlevels[playernum] += levelinc;
                llines -= linesperlevel;
            }
            /* tell everybody else */
            g_snprintf (buf, sizeof(buf), "%d %d",
                        playernum, playerlevels[playernum]);
            client_outmessage (OUT_LVL, buf);
            tetrinet_updatelevels ();
        }
        /* lines to everyone if in classic mode */
        if (classicmode) {
            int sbnum;
            switch (lines) {
            case 2: sbnum = S_ADDALL1; break;
            case 3: sbnum = S_ADDALL2; break;
            case 4: sbnum = S_ADDALL4; break;
            default: goto endremovelines;
            }
            g_snprintf (buf, sizeof(buf), "%i %s %i",
                        0, sbinfo[sbnum].id, playernum);
            client_outmessage (OUT_SB, buf);
            tetrinet_dospecial (playernum, 0, sbnum);
        }
    endremovelines:
        /* end of if */ ;
    }
    /* give it a little delay in drawing */
    sched_timeout_add (40, (sched_timer_func) tetrinet_removelinestimeout,
                   NULL);
    return sound;
}

gint tetrinet_removelinestimeout (void)
{
    tetris_drawcurrentblock ();
    return FALSE;
}

/* return TRUE if key is processed, FALSE if not */
int tetrinet_key (int keyval)
{
    if (spectating) {
        /* spectator keys */
        switch (keyval) {
        case SDLK_1: bigfieldnum = 1; break;
        case SDLK_2: bigfieldnum = 2; break;
        case SDLK_3: bigfieldnum = 3; break;
        case SDLK_4: bigfieldnum = 4; break;
        case SDLK_5: bigfieldnum = 5; break;
        case SDLK_6: bigfieldnum = 6; break;
        default:    goto notfieldkey;
        }
        tetrinet_updatelevels ();
        tetrinet_redrawfields ();
        return TRUE;
    }
notfieldkey:
    if (!ingame) return FALSE;
    if (keyval == keys[K_GAMEMSG]) {
        fields_gmsginput (TRUE);
        gmsgstate = 1;
        tetris_drawcurrentblock ();
        return TRUE;
    }
    if (paused || !playing) return FALSE;
    /* game keys */
    if (keyval == keys[K_ROTRIGHT]) {
        if (!nextblocktimeout)
            sound_playsound (S_ROTATE);
        tetris_blockrotate (1);
        tetris_drawcurrentblock ();
        return TRUE;
    }
    else if (keyval == keys[K_ROTLEFT]) {
        if (!nextblocktimeout)
            sound_playsound (S_ROTATE);
        tetris_blockrotate (-1);
        tetris_drawcurrentblock ();
        return TRUE;
    }
    else if (keyval == keys[K_RIGHT]) {
        tetris_blockmove (1);
        tetris_drawcurrentblock ();
        return TRUE;
    }
    else if (keyval == keys[K_LEFT]) {
        tetris_blockmove (-1);
        tetris_drawcurrentblock ();
        return TRUE;
    }
    else if (keyval == keys[K_DOWN]) {
        if (!downpressed) {
            tetrinet_timeout ();
            downpressed = 1;
            tetrinet_settimeout (DOWNDELAY);
        }
        tetris_drawcurrentblock ();
        return TRUE;
    }
    else if (keyval == keys[K_DROP]) {
        int sound;
        if (!nextblocktimeout) {
            tetris_blockdrop ();
            tetris_solidify ();
            tetrinet_nextblock ();
            sound = tetrinet_removelines();
            if (sound >= 0) sound_playsound (sound);
            else sound_playsound (S_DROP);
            tetrinet_sendfield (0);
        }
        tetris_drawcurrentblock ();
        return TRUE;
    }
    else if (keyval == keys[K_DISCARD]) {
        tetrinet_specialkey(-1);
        tetris_drawcurrentblock ();
        return TRUE;
    }
    else if (keyval == keys[K_SPECIAL1]) {
	tetrinet_specialkey(1);
        tetris_drawcurrentblock ();
        return TRUE;
    }
    else if (keyval == keys[K_SPECIAL2]) {
	tetrinet_specialkey(2);
        tetris_drawcurrentblock ();
        return TRUE;
    }
    else if (keyval == keys[K_SPECIAL3]) {
	tetrinet_specialkey(3);
        tetris_drawcurrentblock ();
        return TRUE;
    }
    else if (keyval == keys[K_SPECIAL4]) {
	tetrinet_specialkey(4);
        tetris_drawcurrentblock ();
        return TRUE;
    }
    else if (keyval == keys[K_SPECIAL5]) {
	tetrinet_specialkey(5);
        tetris_drawcurrentblock ();
        return TRUE;
    }
    else if (keyval == keys[K_SPECIAL6]) {
	tetrinet_specialkey(6);
        tetris_drawcurrentblock ();
        return TRUE;
    }
    tetris_drawcurrentblock ();
    return FALSE;
}

void tetrinet_upkey (int keyval)
{
    if (!playing) return;
    if (keyval == keys[K_DOWN]) {
         /* if it is a quick press, nudge it down one more step */
        if (downpressed == 1) tetrinet_timeout ();
        downpressed = 0;
        tetrinet_settimeout (tetrinet_timeoutduration());
    }
    tetris_drawcurrentblock ();
}

/* called when player uses a special */
void tetrinet_specialkey (int pnum)
{
    char buf[64];
    int sbnum, i;

    if (specialblocknum == 0) return;

    if (pnum != -1 && !playerplaying[pnum]) return;

    /* find which block it is */
    for (sbnum = 0; sbinfo[sbnum].id; sbnum ++)
        if (sbinfo[sbnum].block == specialblocks[0]) break;

    /* remove it from the specials bar */
    for (i = 1; i < specialblocknum; i ++)
        specialblocks[i-1] = specialblocks[i];
    specialblocknum --;
    fields_drawspecials ();
    if (specialblocknum > 0)
        tetrinet_setspeciallabel (specialblocks[0]);
    else
        tetrinet_setspeciallabel (-1);

    /* just discarding a block, no need to say anything */
    if (pnum == -1) return;

    /* send it out */
    g_snprintf (buf, sizeof(buf), "%i %s %i", pnum, sbinfo[sbnum].id, playernum);
    client_outmessage (OUT_SB, buf);

    tetrinet_dospecial (playernum, pnum, sbnum);
}

/******************/
/* misc functions */
/******************/

int mutimeout = 0;

/*
 this function exists to prevent the flood of moderator change messages
 that occur on connecting to a server, by combining them to a single line
 */
int moderatorupdate_timeout (void)
{
    if (moderatornum) {
        char buf[256];
        g_snprintf (buf, sizeof(buf),
                    _("%c*** %c%s%c%c is the moderator"),
                    TETRI_TB_C_BRIGHT_RED, TETRI_TB_BOLD,
                    playernames[moderatornum],
                    TETRI_TB_RESET, TETRI_TB_C_BRIGHT_RED);
        partyline_text (buf);
    }
    mutimeout = 0;
    return FALSE;
}

void moderatorupdate (int now)
{
    if (now) {
        if (mutimeout) {
            sched_timer_remove (mutimeout);
            moderatorupdate_timeout ();
        }
    }
    else {
        if (mutimeout)
            sched_timer_remove (mutimeout);
        mutimeout = sched_timeout_add (PARTYLINEDELAY2,
                                   (sched_timer_func) moderatorupdate_timeout, NULL);
    }
}

/* these functions combines consecutive playerjoin and team messages */
char pjoins[16][128];
char pteams[16][128];
int pcount = 0;
char pleaves[16][128];
int plcount = 0;

int putimeout = 0;

int partylineupdate_timeout (void)
{
    int i, j, c;
    char buf[1024], buf2[1024], team[128];
    int f[16];

    if (plcount) {
        g_snprintf(buf, sizeof(buf), "%c*** ", TETRI_TB_C_DARK_GREEN);
        for (i = 0; i < plcount; i++) {
            g_snprintf (buf2, sizeof(buf2), "%c%s%c%c, ",
                        TETRI_TB_BOLD, pleaves[i],
                        TETRI_TB_RESET, TETRI_TB_C_DARK_GREEN);
            GTET_O_STRCAT (buf, buf2);
        }
        buf[strlen(buf)-2] = 0; /* remove ", " from end of string */
        if (plcount == 1) GTET_O_STRCAT (buf, _(" has left the game"));
        else GTET_O_STRCAT (buf, _(" have left the game"));
        plcount = 0;
        partyline_text (buf);
    }
    if (pcount) {
        g_snprintf(buf, sizeof(buf), "%c*** ", TETRI_TB_C_DARK_GREEN);
        for (i = 0; i < pcount; i++) {
            g_snprintf (buf2, sizeof(buf2), "%c%s%c%c, ",
                        TETRI_TB_BOLD, pjoins[i],
                        TETRI_TB_RESET, TETRI_TB_C_DARK_GREEN);
            GTET_O_STRCAT (buf, buf2);
        }
        buf[strlen(buf)-2] = 0;
        if (pcount == 1) GTET_O_STRCAT (buf, _(" has joined the game"));
        else GTET_O_STRCAT (buf, _(" have joined the game"));
        partyline_text (buf);

        for (i = 0; i < pcount; i ++) f[i] = 1;
        for (i = 0; i < pcount; i ++) if (f[i]) {
            GTET_O_STRCPY (team, pteams[i]);
            g_snprintf (buf, sizeof(buf), "%c*** %c%s%c%c",
                        TETRI_TB_C_DARK_RED, TETRI_TB_BOLD,
                        pjoins[i],
                        TETRI_TB_RESET, TETRI_TB_C_DARK_RED);
            c = 1;
            for (j = i+1; j < pcount; j ++) {
                if (strcmp (team, pteams[j]) == 0) {
                    g_snprintf (buf2, sizeof(buf2), ", %c%s%c%c",
                                TETRI_TB_BOLD, pjoins[j],
                                TETRI_TB_RESET, TETRI_TB_C_DARK_RED);
                    GTET_O_STRCAT (buf, buf2);
                    f[j] = 0;
                    c ++;
                }
            }
            if (0) {}
            else if ((c == 1) &&  team[0])
              partyline_fmt(_("%s is on team %c%s"),
                            buf, TETRI_TB_BOLD, team);
            else if ((c == 1) && !team[0])
              partyline_fmt(_("%s is alone"), buf);
            else if ((c != 1) &&  team[0])
              partyline_fmt(_("%s are on team %c%s"),
                            buf, TETRI_TB_BOLD, team);
            else if ((c != 1) && !team[0])
              partyline_fmt(_("%s are alone"), buf);
        }
        pcount = 0;
    }
    
    putimeout = 0;
    return FALSE;
}

void partylineupdate (int now)
{
    if (putimeout) sched_timer_remove (putimeout);
    if (now)
        partylineupdate_timeout ();
    else
        putimeout = sched_timeout_add (PARTYLINEDELAY1, (sched_timer_func) partylineupdate_timeout,
                                   NULL);
}

void partylineupdate_join (char *name)
{
    int i;
    if (!connected) return;
    for (i = 0; i < pcount; i ++) if (strcmp(pjoins[i], name) == 0) return;
    GTET_O_STRCPY (pjoins[pcount], name);
    pteams[pcount][0] = 0;
    pcount ++;
    partylineupdate (0);
}

void partylineupdate_team (char *name, char *team)
{
    int i;
    if (!connected) return;
    for (i = 0; i < pcount; i ++)
        if (strcmp (pjoins[i], name) == 0) break;
    if (i == pcount) {
        char buf[1024];
        /* player did not just join - display normally */
        if (team[0])
            g_snprintf (buf, sizeof(buf),
                        _("%c*** %c%s%c%c is now on team %c%s"),
                        TETRI_TB_C_DARK_RED, TETRI_TB_BOLD,
                        name,
                        TETRI_TB_RESET, TETRI_TB_C_DARK_RED,
                        TETRI_TB_BOLD, team);
        else
            g_snprintf (buf, sizeof(buf),
                        _("%c*** %c%s%c%c is now alone"),
                        TETRI_TB_C_DARK_RED, TETRI_TB_BOLD,
                        name,
                        TETRI_TB_RESET, TETRI_TB_C_DARK_RED);
        partyline_text (buf);
    }
    GTET_O_STRCPY (pteams[i], team);
    partylineupdate (0);
}

void partylineupdate_leave (char *name)
{
    if (!connected) return;
    GTET_O_STRCPY (pleaves[plcount], name);
    plcount ++;
    partylineupdate (0);
}

void playerlistupdate (void)
{
    int i, sn, n = 0;
    char *pnames[6], *teams[6], *specs[128];
    int pnums[6];
    for (i = 1; i <= 6; i ++) {
        if (playernames[i][0]) {
            pnums[n] = i;
            pnames[n] = playernames[i];
            teams[n] = teamnames[i];
            n ++;
        }
    }
    for (sn = 0; sn < spectatorcount; sn++) specs[sn] = spectatorlist[sn];
    partyline_playerlist (pnums, pnames, teams, n, specs, sn);
}

void fieldslabelupdate (void)
{
    int i;
    for (i = 1; i <= 6; i ++) {
        if (playernames[i][0] == 0)
            fields_setlabel (playerfield(i), NULL, NULL, 0);
        else
            fields_setlabel (playerfield(i), playernames[i], teamnames[i], i);
    }
}

void checkmoderatorstatus (void)
{
    int i;
    /* find the lowest numbered player, or 0 if no players exist */
    for (i = 1; i <= 6; i ++)
        if (playernames[i][0]) break;
    if (i > 6) i = 0;
    if (!spectating && (i == playernum)) moderator = TRUE;
    else moderator = FALSE;
    commands_checkstate ();
    if (moderatornum != i) {
        moderatornum = i;
        moderatorupdate (0);
    }
}

void plinemsg (const char *name, const char *text)
{
    char buf[1024];
    g_snprintf (buf, sizeof(buf), "%c<%s%c%c>%c %s",
                TETRI_TB_BOLD, name,
                TETRI_TB_RESET, TETRI_TB_BOLD, TETRI_TB_BOLD, text);
    partyline_text (buf);
}

void plinesmsg (const char *name, const char *text)
{
    char buf[1024];
    g_snprintf (buf, sizeof(buf), "%c%c<%s%c%c%c>%c %s",
                TETRI_TB_C_BRIGHT_BLUE, TETRI_TB_BOLD,
                name,
                TETRI_TB_RESET,
                TETRI_TB_C_BRIGHT_BLUE, TETRI_TB_BOLD, TETRI_TB_BOLD, text);
    partyline_text (buf);
}

void plineact (const char *name, const char *text)
{
    char buf[1024];
    g_snprintf (buf, sizeof(buf), "%c* %c%s%c%c %s",
                TETRI_TB_C_PURPLE, TETRI_TB_BOLD, name,
                TETRI_TB_RESET, TETRI_TB_C_PURPLE, text);
    partyline_text (buf);
}

void plinesact (const char *name, const char *text)
{
    char buf[1024];
    g_snprintf (buf, sizeof(buf), "%c* %c%s%c%c %s",
                TETRI_TB_C_BRIGHT_BLUE, TETRI_TB_BOLD,
                name,
                TETRI_TB_RESET, TETRI_TB_C_BRIGHT_BLUE, text);
    partyline_text (buf);
}

char translateblock (char c)
{
    int i;
    for (i = 0; blocks[i]; i ++)
        if (c == blocks[i]) return i;
    return 0;
}

int playerfield (int p)
{
    if (p < bigfieldnum) return p;
    else if (p == bigfieldnum) return 0;
    else return p - 1;
}

void clearallfields (void)
{
    int i;
    memset (fields, 0, 7*FIELDHEIGHT*FIELDWIDTH);
    for (i = 1; i <= 6; i ++)
        fields_drawfield (playerfield(i), fields[i]);
}

void speclist_clear (void)
{
    spectatorcount = 0;
}

void speclist_add (char *name)
{
    int p, i;
    
    if (spectatorcount == (sizeof(spectatorlist) / sizeof(spectatorlist[0])))
      return;
    
    for (p = 0; p < spectatorcount; p++)
        if (strcasecmp(name, spectatorlist[p]) < 0) break;
    for (i = spectatorcount; i > p; i--)
        GTET_O_STRCPY (spectatorlist[i], spectatorlist[i-1]);
    GTET_O_STRCPY (spectatorlist[p], name);
    spectatorcount ++;
}

void speclist_remove (char *name)
{
    int i;
    for (i = 0; i < spectatorcount; i ++) {
        if (strcmp(name, spectatorlist[i]) == 0) {
            for (; i < spectatorcount-1; i++)
                GTET_O_STRCPY (spectatorlist[i], spectatorlist[i+1]);
            spectatorcount --;
            return;
        }
    }
}
