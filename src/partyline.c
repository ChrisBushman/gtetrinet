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
#include <stdarg.h>
#include <time.h>
#include <glib.h>
#include <SDL.h>

#include "client.h"
#include "tetrinet.h"
#include "partyline.h"
#include "misc.h"

int timestampsenable;
int list_enabled;

static int entrybox_enabled;
static char namelabel_text[128];
static char teamlabel_text[128];
static char infolabel_text[256];
static char joining_channel_text[256];

static T_textlog chatlog;

#define PARTYLINE_ENTRY_MAXLEN 511
static char entry_buf[PARTYLINE_ENTRY_MAXLEN + 1];
static int entry_len;

/* pline (chat entry) history -- pure logic, unchanged from the original
 * other than storage: a ring buffer of past entries, browsed with
 * Up/Down. */
#define PLHSIZE 64
static char plhistory[PLHSIZE][256];
static int plh_start = 0, plh_end = 0, plh_cur = 0;

typedef struct {
    char number[16];
    char name[128];
    char team[128];
} T_plplayer;

#define MAX_PL_PLAYERS 16
static T_plplayer pl_players[MAX_PL_PLAYERS];
static int pl_player_count;

#define MAX_PL_SPECS 32
static char pl_specs[MAX_PL_SPECS][128];
static int pl_spec_count;

typedef struct {
    int num;
    char name[256];
    char players[64];
    char state[64];
    char desc[512];
} T_channel;

#define MAX_CHANNELS 128
static T_channel work_channels[MAX_CHANNELS];
static int work_channel_count;
static T_channel display_channels[MAX_CHANNELS];
static int display_channel_count;

int partyline_page_new (void)
{
    entrybox_enabled = 0;
    namelabel_text[0] = teamlabel_text[0] = infolabel_text[0] = joining_channel_text[0] = 0;
    misc_textlog_clear (&chatlog);
    entry_buf[0] = 0;
    entry_len = 0;
    plhistory[0][0] = 0;
    plh_start = plh_end = plh_cur = 0;
    pl_player_count = pl_spec_count = 0;
    work_channel_count = display_channel_count = 0;

    partyline_connectstatus (FALSE);

    return 0;
}

void partyline_page_cleanup (void)
{
    /* Nothing owned here needs freeing (no loaded images/fonts, unlike
     * fields.c/winlist.c) -- kept for API symmetry with those. */
}

void partyline_connectstatus (int status)
{
    entrybox_enabled = status ? 1 : 0;
}

void partyline_namelabel (char *nick, char *team)
{
    GTET_O_STRCPY (namelabel_text, nick ? nick : "");
    GTET_O_STRCPY (teamlabel_text, team ? team : "");
}

void partyline_status (char *status)
{
    GTET_O_STRCPY (infolabel_text, status);
}

void partyline_text (const char *text)
{
    if (timestampsenable) {
        time_t now;
        char buf[1024];
        char timestamp[9];

        now = time (NULL);

        strftime (timestamp, sizeof (timestamp), "%H:%M:%S", localtime (&now));
        g_snprintf (buf, sizeof (buf), "%c[%s]%c %s",
                    TETRI_TB_C_GREY, timestamp, TETRI_TB_RESET, text);

        misc_textlog_append (&chatlog, buf);
    }
    else
        misc_textlog_append (&chatlog, text);
}

void partyline_fmt (const char *fmt, ...)
{
    va_list ap;
    char *text = NULL;

    va_start (ap, fmt);
    text = g_strdup_vprintf (fmt, ap);
    va_end (ap);

    partyline_text (text);
    g_free (text);
}

void partyline_playerlist (int *numbers, char **names, char **teams, int n, char **specs, int sn)
{
    int i;

    pl_player_count = 0;
    for (i = 0; i < n && pl_player_count < MAX_PL_PLAYERS; i++) {
        T_plplayer *p = &pl_players[pl_player_count++];
        g_snprintf (p->number, sizeof (p->number), "%d", numbers[i]);
        GTET_O_STRCPY (p->name, nocolor (names[i]));
        GTET_O_STRCPY (p->team, nocolor (teams[i]));
    }

    pl_spec_count = 0;
    for (i = 0; i < sn && pl_spec_count < MAX_PL_SPECS; i++)
        GTET_O_STRCPY (pl_specs[pl_spec_count++], nocolor (specs[i]));
}

void partyline_entryfocus (void)
{
    if (connected) {
        entry_buf[0] = 0;
        entry_len = 0;
    }
}

/* --- pline history + submit, replacing GtkEntry's "activate" signal
   handler (textentry()) and the Up/Down/Tab cases of entrykey() --- */

static void
submit_entry (void)
{
    if (entry_len == 0)
        return;

    if (g_str_has_prefix (entry_buf, "/list"))
        stop_list (); /* Parsing can't be perfect, so make sure they can do it by hand... */

    /* Show the command if it's a /msg */
    if (g_str_has_prefix (entry_buf, "/msg"))
        partyline_text (entry_buf);

    tetrinet_playerline (entry_buf);
    GTET_STRCPY (plhistory[plh_end], entry_buf, sizeof (plhistory[0]));
    entry_buf[0] = 0;
    entry_len = 0;

    plh_end++;
    if (plh_end == PLHSIZE) plh_end = 0;
    if (plh_end == plh_start) plh_start++;
    if (plh_start == PLHSIZE) plh_start = 0;
    plh_cur = plh_end;
}

static void
history_recall (int up)
{
    if (plh_cur == plh_end)
        GTET_STRCPY (plhistory[plh_end], entry_buf, sizeof (plhistory[0]));

    if (up) {
        if (plh_cur == plh_start) return;
        plh_cur--;
        if (plh_cur == -1) plh_cur = PLHSIZE - 1;
    } else {
        if (plh_cur == plh_end) return;
        plh_cur++;
        if (plh_cur == PLHSIZE) plh_cur = 0;
    }

    GTET_STRCPY (entry_buf, plhistory[plh_cur], sizeof (entry_buf));
    entry_len = (int) strlen (entry_buf);
}

/* Replacing is_nick()/playerlist_complete_nick(): searches players then
 * spectators (matching the original's single combined GtkListStore
 * iteration order -- players first, blank separator, then specs) for
 * the first name whose lowercased form starts with the already-typed
 * lowercased prefix, and replaces the entry with "Name: " on a match. */
static void
complete_nick (void)
{
    gchar *typed;
    int i;

    typed = g_utf8_strdown (entry_buf, -1);
    if (typed == NULL)
        return;

    for (i = 0; i < pl_player_count; i++) {
        gchar *down = g_utf8_strdown (pl_players[i].name, -1);
        int match = g_str_has_prefix (down, typed);
        g_free (down);
        if (match) {
            g_snprintf (entry_buf, sizeof (entry_buf), "%s: ", pl_players[i].name);
            entry_len = (int) strlen (entry_buf);
            g_free (typed);
            return;
        }
    }
    for (i = 0; i < pl_spec_count; i++) {
        gchar *down = g_utf8_strdown (pl_specs[i], -1);
        int match = g_str_has_prefix (down, typed);
        g_free (down);
        if (match) {
            g_snprintf (entry_buf, sizeof (entry_buf), "%s: ", pl_specs[i]);
            entry_len = (int) strlen (entry_buf);
            g_free (typed);
            return;
        }
    }
    g_free (typed);
}

void partyline_textinput (const char *text)
{
    size_t addlen;

    if (!entrybox_enabled || text == NULL)
        return;
    addlen = strlen (text);
    if (entry_len + addlen > PARTYLINE_ENTRY_MAXLEN)
        addlen = PARTYLINE_ENTRY_MAXLEN - entry_len;
    if ((int) addlen <= 0)
        return;
    memcpy (entry_buf + entry_len, text, addlen);
    entry_len += (int) addlen;
    entry_buf[entry_len] = 0;

    /* Typing normally resets history browsing back to "current", same
     * as entrykey()'s final else-branch did. */
    plh_cur = plh_end;
}

void partyline_backspace (void)
{
    if (!entrybox_enabled || entry_len == 0)
        return;
    entry_len--;
    while (entry_len > 0 && ((unsigned char) entry_buf[entry_len] & 0xC0) == 0x80)
        entry_len--;
    entry_buf[entry_len] = 0;
}

void partyline_keydown (int keycode)
{
    if (!entrybox_enabled)
        return;

    if (keycode == SDLK_UP)
        history_recall (1);
    else if (keycode == SDLK_DOWN)
        history_recall (0);
    else if (keycode == SDLK_TAB)
        complete_nick ();
    else if (keycode == SDLK_RETURN || keycode == SDLK_KP_ENTER)
        submit_entry ();
    /* Left/Right (mid-string cursor movement) are not implemented in
     * this pass, matching fields.c's gmsg input having the same
     * append/backspace-only scope -- a known simplification, not a
     * silently dropped feature. */
}

const char *partyline_entrytext (void)
{
    return entry_buf;
}

/* --- channel list: GScanner-based line parsing is pure protocol/text
   logic (portable glib, no GTK) and kept close to verbatim; only the
   storage (work_channels[]/display_channels[] arrays instead of
   GtkListStore) changed. --- */

void partyline_add_channel (char *line)
{
    GScanner *scan;
    gint num, actual, max;
    gchar *name = NULL, *players = NULL, *state = NULL, final[1024], *desc = NULL, *utf8 = NULL;
    T_channel *chan;

    if (work_channel_count >= MAX_CHANNELS)
        return;

    scan = g_scanner_new (NULL);
    g_scanner_input_text (scan, line, strlen (line));

    scan->config->cpair_comment_single = ""; /* in jetrix, channels don't start with a '#' in list; use [ to start another token after channel name (tetrinet-server does not leave a space before [) */
    scan->config->skip_comment_single = FALSE;
    scan->config->cset_skip_characters = " \n\t[";
    scan->config->scan_identifier_1char = TRUE;

    while ((g_scanner_get_next_token (scan) != G_TOKEN_INT) && !g_scanner_eof (scan));
    num = (scan->token == G_TOKEN_INT) ? scan->value.v_int : 0;

    g_scanner_get_next_token (scan); /* dump the ')' */
    scan->config->cset_identifier_first = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"; /* tokens can start with any character, but as identifiers take precedence, we can't detect INT anymore */

    if (g_scanner_peek_next_token (scan) == G_TOKEN_LEFT_BRACE)
    {
        scan->config->cpair_comment_single = "# ";

        while ((g_scanner_get_next_token (scan) != G_TOKEN_INT) && !g_scanner_eof (scan));
        actual = (scan->token == G_TOKEN_INT) ? scan->value.v_int : 0;

        while ((g_scanner_get_next_token (scan) != G_TOKEN_INT) && !g_scanner_eof (scan));
        max = (scan->token == G_TOKEN_INT) ? scan->value.v_int : 0;

        while ((g_scanner_get_next_token (scan) != G_TOKEN_COMMENT_SINGLE) && !g_scanner_eof (scan));
        /* This will be utf-8 since it's converted in client_readmsg, but just in
         * case the parsing code splits up a char sequence.. - vidar
         */
        utf8 = ensure_utf8 ((scan->token == G_TOKEN_COMMENT_SINGLE) ? scan->value.v_comment : "");
        name = g_strconcat ("#", utf8, NULL);

        g_snprintf (final, 1024, "%d/%d", actual, max);

        scan->config->cpair_comment_single = "{}";
        while ((g_scanner_get_next_token (scan) != G_TOKEN_COMMENT_SINGLE) && !g_scanner_eof (scan));
        if (!g_scanner_eof (scan))
            state = g_strdup ((scan->token == G_TOKEN_COMMENT_SINGLE) ? scan->value.v_comment : "");
        else
            state = g_strdup ("IDLE");

        desc = g_strdup ("");
        players = g_strdup ("");
    }
    else /* tetrinet-server & jetrix */
    {
        while ((g_scanner_get_next_token (scan) != G_TOKEN_IDENTIFIER) && !g_scanner_eof (scan)); /* in jetrix, channels don't start with a '#' in list, this supports channels starting with and without '#' */
        utf8 = ensure_utf8 ((scan->token == G_TOKEN_IDENTIFIER) ? scan->value.v_identifier : "");
        name = g_strconcat ("#", utf8, NULL);

        while ((g_scanner_get_next_token (scan) != G_TOKEN_IDENTIFIER) && !g_scanner_eof (scan));
        players = g_strdup ((scan->token == G_TOKEN_IDENTIFIER) ? scan->value.v_identifier : "");

        if (players != NULL)
        {
            if (strncmp (players, "FULL", 4))
            {
                scan->config->cset_identifier_first = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
                while ((g_scanner_get_next_token (scan) != G_TOKEN_INT) && !g_scanner_eof (scan));
                actual = (scan->token == G_TOKEN_INT) ? scan->value.v_int : 0;

                while ((g_scanner_get_next_token (scan) != G_TOKEN_INT) && !g_scanner_eof (scan));
                max = (scan->token == G_TOKEN_INT) ? scan->value.v_int : 0;

                g_snprintf (final, 1024, "%d/%d %s", actual, max, players);
                scan->config->cset_identifier_first = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
            }
            else
                g_snprintf (final, 1024, "%s", players);
        }
        else
            g_snprintf (final, 1024, "UNK");

        g_scanner_get_next_token (scan); /* dump the ']' */

        scan->config->cpair_comment_single = "{}";
        while ((g_scanner_get_next_token (scan) != G_TOKEN_COMMENT_SINGLE) && !g_scanner_eof (scan));
        if (!g_scanner_eof (scan))
            state = g_strdup ((scan->token == G_TOKEN_COMMENT_SINGLE) ? scan->value.v_comment : ""); /* INGAME */
        else
            state = g_strdup ("IDLE");

        /* give us the rest of the line */
        if (!g_scanner_eof (scan) && (scan->position < strlen (line)))
            desc = g_strstrip (ensure_utf8 (&line[scan->position]));
        else
            desc = g_strdup ("");
    }

    chan = &work_channels[work_channel_count++];
    chan->num = num;
    GTET_STRCPY (chan->name, name ? name : "", sizeof (chan->name));
    GTET_STRCPY (chan->players, final, sizeof (chan->players));
    GTET_STRCPY (chan->state, state ? state : "", sizeof (chan->state));
    GTET_STRCPY (chan->desc, desc ? desc : "", sizeof (chan->desc));

    g_scanner_destroy (scan);
    g_free (name);
    g_free (state);
    g_free (players);
    g_free (desc);
    g_free (utf8);
}

void stop_list (void)
{
    int i;

    list_issued = 0;

    /* "double buffering": copy the staging list built up by
     * partyline_add_channel() into the displayed list in one shot,
     * rather than the displayed list flickering/partially updating
     * while a multi-line /list response streams in. work_channels[]
     * itself is deliberately NOT cleared here, matching the original
     * (only partyline_update_channel_list()/partyline_clear_list_channel()
     * clear the staging model). */
    display_channel_count = 0;
    for (i = 0; i < work_channel_count && display_channel_count < MAX_CHANNELS; i++)
        display_channels[display_channel_count++] = work_channels[i];
}

int partyline_update_channel_list (void)
{
    char buf[1024];

    /* if there is another update in progress, just go away silently */
    if (connected && list_enabled && (list_issued == 0)) {
        list_issued++;
        work_channel_count = 0;
        tetrinet_playerline ("/list");

        /* send the mark */
        g_snprintf (buf, sizeof (buf), "/msg %d --- MARK ---", playernum);
        tetrinet_playerline (buf);
    }

    return TRUE; /* keep the 30-second sched_timeout_add repeating (see
                    tetrinet.c) */
}

void partyline_more_channel_lines (void)
{
    char buf[1024];

    list_issued++;
    tetrinet_playerline ("/list+");
    g_snprintf (buf, sizeof (buf), "/msg %d --- MARK ---", playernum);
    tetrinet_playerline (buf);
}

void partyline_clear_list_channel (void)
{
    display_channel_count = 0;
    work_channel_count = 0;
}

/* Replaces channel_activated(), which fired on a GtkTreeView row
 * double-click/Enter. The main loop (not yet written) calls this with
 * the clicked row's index into the currently-displayed channel list once
 * it has mouse-hit-testing against partyline_render()'s layout. */
void partyline_channel_activate (int index)
{
    char buf[300];

    if (index < 0 || index >= display_channel_count)
        return;
    g_snprintf (buf, sizeof (buf), "/join %s", display_channels[index].name);
    tetrinet_playerline (buf);
}

void partyline_joining_channel (const char *channel)
{
    if (channel != NULL)
        g_snprintf (joining_channel_text, sizeof (joining_channel_text), "Talking in channel %s", channel);
    else
        GTET_O_STRCPY (joining_channel_text, "Disconnected");
}

void partyline_show_channel_list (int show)
{
    list_enabled = show;
    if (list_enabled)
        partyline_update_channel_list ();
}

/* --- rendering --- */

static void
render_label (SDL_Surface *dst, int x, int y, const char *text)
{
    T_textstyle style;

    style.color.r = style.color.g = style.color.b = 0xFF;
    style.bold = style.italic = style.underline = 0;
    misc_font_render (dst, x, y, &style, text, strlen (text));
}

static void
render_label_bold (SDL_Surface *dst, int x, int y, const char *text)
{
    T_textstyle style;

    style.color.r = style.color.g = style.color.b = 0xFF;
    style.bold = 1; style.italic = style.underline = 0;
    misc_font_render (dst, x, y, &style, text, strlen (text));
}

/* infolabel_text can contain an embedded '\n' (see partyline_status()'s
 * "Connected to\n<server>" caller) -- misc_font_render() has no line-break
 * handling of its own, so a literal '\n' byte was rasterized as a missing-
 * glyph box between the two halves of the message. Split it into separate
 * render_label() calls instead; label_h in partyline_render() already
 * reserves 2 line-heights for this. */
static void
render_label_lines (SDL_Surface *dst, int x, int y, const char *text)
{
    int line_h = misc_font_line_height ();
    const char *line_start = text;
    const char *nl;

    while ((nl = strchr (line_start, '\n')) != NULL) {
        char buf[256];
        size_t len = (size_t) (nl - line_start);

        if (len >= sizeof (buf))
            len = sizeof (buf) - 1;
        memcpy (buf, line_start, len);
        buf[len] = 0;
        render_label (dst, x, y, buf);
        y += line_h;
        line_start = nl + 1;
    }
    render_label (dst, x, y, line_start);
}

static void
render_player_list (SDL_Surface *dst, const SDL_Rect *rect)
{
    int line_h = misc_font_line_height ();
    int y = rect->y;
    int i;
    char buf[300];

    for (i = 0; i < pl_player_count && y + line_h <= rect->y + rect->h; i++) {
        if (pl_players[i].team[0])
            g_snprintf (buf, sizeof (buf), "%s: %s (%s)", pl_players[i].number, pl_players[i].name, pl_players[i].team);
        else
            g_snprintf (buf, sizeof (buf), "%s: %s", pl_players[i].number, pl_players[i].name);
        render_label (dst, rect->x, y, buf);
        y += line_h;
    }
    y += line_h / 2;
    for (i = 0; i < pl_spec_count && y + line_h <= rect->y + rect->h; i++) {
        g_snprintf (buf, sizeof (buf), "(spec) %s", pl_specs[i]);
        render_label (dst, rect->x, y, buf);
        y += line_h;
    }
}

static void
render_channel_list (SDL_Surface *dst, const SDL_Rect *rect)
{
    int line_h = misc_font_line_height ();
    int y = rect->y;
    int i;
    char buf[900];

    for (i = 0; i < display_channel_count && y + line_h <= rect->y + rect->h; i++) {
        g_snprintf (buf, sizeof (buf), "%s [%s] %s %s",
                    display_channels[i].name, display_channels[i].players,
                    display_channels[i].state, display_channels[i].desc);
        render_label (dst, rect->x, y, buf);
        y += line_h;
    }
}

/* Lays out, inside rect: name/team/info/joining-channel labels along
 * the top; chat log + entry box in a left column; player list (and,
 * if enabled, the channel list below it) in a right column. Recomputed
 * every call rather than cached like fields.c's BLOCKSIZE-dependent
 * layout -- these proportions don't depend on anything that changes at
 * runtime, so there's nothing to invalidate/recompute-on-change for. */
void partyline_render (SDL_Surface *dst, const SDL_Rect *rect)
{
    int line_h = misc_font_line_height ();
    int label_h = line_h * 2 + 4;
    int right_w = rect->w * 3 / 10;
    int left_w = rect->w - right_w - 8;
    SDL_Rect chat_rect, input_rect, player_rect, channel_rect;
    char buf[300];

    render_label_bold (dst, rect->x, rect->y, namelabel_text);
    if (teamlabel_text[0]) {
        char teambuf[160];
        g_snprintf (teambuf, sizeof (teambuf), "(%s)", teamlabel_text);
        render_label (dst, rect->x + 200, rect->y, teambuf);
    }
    render_label_lines (dst, rect->x, rect->y + line_h, infolabel_text);
    /* Second row, not the first: the name/team labels above have no
     * fixed width available without a text-measuring API (misc.h only
     * exposes render + line-height), so sharing a row with them risks
     * overlap the same way fields.c's field labels can (documented
     * there too). The second row only has the short info label on the
     * left, leaving room on the right. */
    if (joining_channel_text[0])
        render_label_bold (dst, rect->x + left_w - 220, rect->y + line_h, joining_channel_text);

    chat_rect.x = rect->x;
    chat_rect.y = rect->y + label_h;
    chat_rect.w = left_w;
    chat_rect.h = rect->h - label_h - line_h - 8;
    misc_textlog_render (dst, &chat_rect, &chatlog);

    input_rect.x = rect->x;
    input_rect.y = chat_rect.y + chat_rect.h + 4;
    input_rect.w = left_w;
    input_rect.h = line_h + 4;
    SDL_FillRect (dst, &input_rect, SDL_MapRGB (dst->format, 32, 32, 32));
    if (entrybox_enabled) {
        g_snprintf (buf, sizeof (buf), "%s", entry_buf);
        render_label (dst, input_rect.x + 2, input_rect.y + 2, buf);
    }

    player_rect.x = rect->x + left_w + 8;
    player_rect.y = rect->y + label_h;
    player_rect.w = right_w;
    if (list_enabled) {
        player_rect.h = (rect->h - label_h) / 2;
        channel_rect.x = player_rect.x;
        channel_rect.y = player_rect.y + player_rect.h + 4;
        channel_rect.w = right_w;
        channel_rect.h = rect->h - label_h - player_rect.h - 4;
        render_channel_list (dst, &channel_rect);
    } else {
        player_rect.h = rect->h - label_h;
    }
    render_player_list (dst, &player_rect);
}
