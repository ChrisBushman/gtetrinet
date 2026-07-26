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
#include <sys/types.h>
#include <dirent.h>
#include <glib.h>
#include <SDL.h>

#include "gtet_config.h"
#include "client.h"
#include "tetrinet.h"
#include "misc.h"
#include "sound.h"
#include "partyline.h"
#include "dialogs.h"

/* Forward declarations instead of pulling in the still-GTK gtetrinet.h,
 * same approach config.c/commands.c already use. */
extern int gamemode;
#define ORIGINAL 0
#define TETRIFAST 1

#define DLG_PAD 8

/* --- shared drawing/hit-test helpers -------------------------------- */

static void
fill_box (SDL_Surface *dst, const SDL_Rect *r, Uint8 gr, Uint8 gg, Uint8 gb)
{
    SDL_FillRect (dst, r, SDL_MapRGB (dst->format, gr, gg, gb));
}

static void
draw_border (SDL_Surface *dst, const SDL_Rect *r, Uint8 gr, Uint8 gg, Uint8 gb)
{
    Uint32 c = SDL_MapRGB (dst->format, gr, gg, gb);
    SDL_Rect top = { r->x, r->y, r->w, 1 };
    SDL_Rect bottom = { r->x, r->y + r->h - 1, r->w, 1 };
    SDL_Rect left = { r->x, r->y, 1, r->h };
    SDL_Rect right = { r->x + r->w - 1, r->y, 1, r->h };
    SDL_FillRect (dst, &top, c);
    SDL_FillRect (dst, &bottom, c);
    SDL_FillRect (dst, &left, c);
    SDL_FillRect (dst, &right, c);
}

static void
draw_text (SDL_Surface *dst, int x, int y, const char *text, int bold)
{
    T_textstyle style;
    style.color.r = style.color.g = style.color.b = 0xFF;
    style.color.a = 0xFF;
    style.bold = bold;
    style.italic = style.underline = 0;
    misc_font_render (dst, x, y, &style, text, strlen (text));
}

static void
draw_text_muted (SDL_Surface *dst, int x, int y, const char *text)
{
    T_textstyle style;
    style.color.r = style.color.g = style.color.b = 0xA0;
    style.color.a = 0xFF;
    style.bold = style.italic = style.underline = 0;
    misc_font_render (dst, x, y, &style, text, strlen (text));
}

static void
draw_text_error (SDL_Surface *dst, int x, int y, const char *text)
{
    T_textstyle style;
    style.color.r = 0xFF; style.color.g = 0x60; style.color.b = 0x60;
    style.color.a = 0xFF;
    style.bold = style.italic = style.underline = 0;
    misc_font_render (dst, x, y, &style, text, strlen (text));
}

static void
draw_box (SDL_Surface *dst, const SDL_Rect *r, const char *title)
{
    fill_box (dst, r, 28, 28, 28);
    draw_border (dst, r, 110, 110, 110);
    if (title)
        draw_text (dst, r->x + DLG_PAD, r->y + DLG_PAD / 2, title, 1);
}

static void
draw_field (SDL_Surface *dst, const SDL_Rect *r, const char *text, int focused, int enabled)
{
    Uint8 bg = !enabled ? 20 : (focused ? 50 : 34);
    fill_box (dst, r, bg, bg, bg);
    draw_border (dst, r, enabled ? 120 : 60, enabled ? 120 : 60, enabled ? 120 : 60);
    if (enabled)
        draw_text (dst, r->x + 4, r->y + 2, text, 0);
    else
        draw_text_muted (dst, r->x + 4, r->y + 2, text);
}

static void
draw_checkbox (SDL_Surface *dst, const SDL_Rect *r, const char *label, int checked)
{
    fill_box (dst, r, 20, 20, 20);
    draw_border (dst, r, 130, 130, 130);
    if (checked) {
        SDL_Rect inner = { r->x + 3, r->y + 3, r->w - 6, r->h - 6 };
        fill_box (dst, &inner, 200, 200, 200);
    }
    draw_text (dst, r->x + r->w + 8, r->y - 2, label, 0);
}

static void
draw_radio (SDL_Surface *dst, const SDL_Rect *r, const char *label, int selected)
{
    fill_box (dst, r, 20, 20, 20);
    draw_border (dst, r, 130, 130, 130);
    if (selected) {
        SDL_Rect inner = { r->x + 3, r->y + 3, r->w - 6, r->h - 6 };
        fill_box (dst, &inner, 200, 200, 200);
    }
    draw_text (dst, r->x + r->w + 8, r->y - 2, label, 0);
}

static void
draw_button (SDL_Surface *dst, const SDL_Rect *r, const char *label, int enabled)
{
    Uint8 bg = enabled ? 60 : 40;
    fill_box (dst, r, bg, bg, bg);
    draw_border (dst, r, 130, 130, 130);
    draw_text (dst, r->x + 8, r->y + (r->h - misc_font_line_height ()) / 2, label, 0);
}

static int
point_in_rect (int x, int y, const SDL_Rect *r)
{
    return x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h;
}

/*****************************************************/
/* connecting dialog - a dialog with a cancel button  */
/*****************************************************/

static int connectingdialog_open;
static Uint32 connecting_last_tick;
static int connecting_pulse_pos;
static SDL_Rect connecting_box, connecting_bar, connecting_cancel_button;

static void
connectingdialog_layout (const SDL_Rect *rect)
{
    int line_h = misc_font_line_height ();
    int w = 260, h = 90;

    connecting_box.x = rect->x + (rect->w - w) / 2;
    connecting_box.y = rect->y + (rect->h - h) / 2;
    connecting_box.w = w;
    connecting_box.h = h;

    connecting_bar.x = connecting_box.x + DLG_PAD;
    connecting_bar.y = connecting_box.y + DLG_PAD + line_h + 8;
    connecting_bar.w = w - DLG_PAD * 2;
    connecting_bar.h = 16;

    connecting_cancel_button.w = 80;
    connecting_cancel_button.h = line_h + 8;
    connecting_cancel_button.x = connecting_box.x + (w - 80) / 2;
    connecting_cancel_button.y = connecting_box.y + h - DLG_PAD - connecting_cancel_button.h;
}

void
connectingdialog_new (void)
{
    if (connectingdialog_open)
        return;
    connectingdialog_open = 1;
    connecting_pulse_pos = 0;
    connecting_last_tick = SDL_GetTicks ();
}

void
connectingdialog_destroy (void)
{
    connectingdialog_open = 0;
}

static void
connectingdialog_cancel (void)
{
    client_disconnect ();
    connectingdialog_destroy ();
}

static void
connectingdialog_tick (void)
{
    Uint32 now;

    if (!connectingdialog_open)
        return;

    now = SDL_GetTicks ();
    if (now - connecting_last_tick >= 20) {
        connecting_last_tick = now;
        connecting_pulse_pos = (connecting_pulse_pos + 4) % 200;
    }
}

static void
connectingdialog_render (SDL_Surface *dst, const SDL_Rect *rect)
{
    SDL_Rect fill;
    int pos;

    connectingdialog_layout (rect);
    draw_box (dst, &connecting_box, "Connect to server");

    fill_box (dst, &connecting_bar, 15, 15, 15);
    draw_border (dst, &connecting_bar, 90, 90, 90);
    pos = connecting_pulse_pos;
    if (pos > 100)
        pos = 200 - pos;
    fill.x = connecting_bar.x + (connecting_bar.w - 40) * pos / 100;
    fill.y = connecting_bar.y + 2;
    fill.w = 40;
    fill.h = connecting_bar.h - 4;
    fill_box (dst, &fill, 150, 150, 220);

    draw_button (dst, &connecting_cancel_button, "Cancel", 1);
}

static void
connectingdialog_click (int x, int y)
{
    if (point_in_rect (x, y, &connecting_cancel_button))
        connectingdialog_cancel ();
}

/*******************/
/* the team dialog */
/*******************/

static int team_dialog_open;
static char team_buf[128];
static int team_len;
static SDL_Rect team_box, team_field_rect, team_ok_button, team_cancel_button;

static void
teamdialog_layout (const SDL_Rect *rect)
{
    int line_h = misc_font_line_height ();
    int w = 300, h = 120;

    team_box.x = rect->x + (rect->w - w) / 2;
    team_box.y = rect->y + (rect->h - h) / 2;
    team_box.w = w;
    team_box.h = h;

    team_field_rect.x = team_box.x + DLG_PAD;
    /* Title clearance (line_h + 8), then the "Team name:" label's own
       line, then the field -- draw_field()'s caller previously put the
       label directly above the field without budgeting for the title
       row too, so the two text lines overlapped. */
    team_field_rect.y = team_box.y + DLG_PAD + (line_h + 8) + (line_h + 4);
    team_field_rect.w = w - DLG_PAD * 2;
    team_field_rect.h = line_h + 4;

    team_ok_button.w = 80;
    team_ok_button.h = line_h + 8;
    team_ok_button.x = team_box.x + w - DLG_PAD - 80;
    team_ok_button.y = team_box.y + h - DLG_PAD - team_ok_button.h;
    team_cancel_button = team_ok_button;
    team_cancel_button.x -= 90;
}

void
teamdialog_new (void)
{
    if (team_dialog_open)
        return;
    team_dialog_open = 1;
    GTET_O_STRCPY (team_buf, team);
    team_len = (int) strlen (team_buf);
}

static void
teamdialog_close (void)
{
    team_dialog_open = 0;
}

static void
teamdialog_ok (void)
{
    GTET_O_STRCPY (team, team_buf);
    config_set_team (team_buf);
    tetrinet_changeteam (team_buf);
    teamdialog_close ();
}

static void
teamdialog_render (SDL_Surface *dst, const SDL_Rect *rect)
{
    teamdialog_layout (rect);
    draw_box (dst, &team_box, "Change team");
    draw_text (dst, team_box.x + DLG_PAD, team_field_rect.y - misc_font_line_height () - 2, "Team name:", 0);
    draw_field (dst, &team_field_rect, team_buf, 1, 1);
    draw_button (dst, &team_cancel_button, "Cancel", 1);
    draw_button (dst, &team_ok_button, "OK", 1);
}

static void
teamdialog_click (int x, int y)
{
    if (point_in_rect (x, y, &team_ok_button))
        teamdialog_ok ();
    else if (point_in_rect (x, y, &team_cancel_button))
        teamdialog_close ();
}

static void
teamdialog_textinput (const char *text)
{
    size_t addlen = strlen (text);

    if (team_len + addlen > sizeof (team_buf) - 1)
        addlen = sizeof (team_buf) - 1 - team_len;
    if ((int) addlen <= 0)
        return;
    memcpy (team_buf + team_len, text, addlen);
    team_len += (int) addlen;
    team_buf[team_len] = 0;
}

static void
teamdialog_backspace (void)
{
    if (team_len == 0)
        return;
    team_len--;
    while (team_len > 0 && ((unsigned char) team_buf[team_len] & 0xC0) == 0x80)
        team_len--;
    team_buf[team_len] = 0;
}

static void
teamdialog_keydown (int keycode)
{
    if (keycode == SDLK_RETURN || keycode == SDLK_KP_ENTER)
        teamdialog_ok ();
    else if (keycode == SDLK_ESCAPE)
        teamdialog_close ();
}

/**********************/
/* the connect dialog */
/**********************/

typedef enum {
    CONNECT_FOCUS_SERVER,
    CONNECT_FOCUS_NICK,
    CONNECT_FOCUS_TEAM,
    CONNECT_FOCUS_PASSWORD
} T_connect_focus;

static int connectdialog_open;
static char connect_server_buf[256];
static char connect_nick_buf[128];
static char connect_team_buf[128];
static char connect_password_buf[128];
static int connect_spectator;
static T_connect_focus connect_focus;
static char connect_error[256];

static SDL_Rect connect_box;
static SDL_Rect connect_server_field, connect_nick_field, connect_team_field, connect_password_field;
static SDL_Rect connect_spectator_check, connect_original_radio, connect_tetrifast_radio;
static SDL_Rect connect_ok_button, connect_cancel_button;

static char *
connect_focus_buf (T_connect_focus f, int *len_out)
{
    switch (f) {
    case CONNECT_FOCUS_SERVER:   *len_out = (int) strlen (connect_server_buf);   return connect_server_buf;
    case CONNECT_FOCUS_NICK:     *len_out = (int) strlen (connect_nick_buf);     return connect_nick_buf;
    case CONNECT_FOCUS_TEAM:     *len_out = (int) strlen (connect_team_buf);     return connect_team_buf;
    case CONNECT_FOCUS_PASSWORD: *len_out = (int) strlen (connect_password_buf); return connect_password_buf;
    }
    *len_out = 0;
    return connect_server_buf;
}

static void
connectdialog_layout (const SDL_Rect *rect)
{
    int line_h = misc_font_line_height ();
    int w = 420, h = 300;
    int x, y, field_w;

    connect_box.x = rect->x + (rect->w - w) / 2;
    connect_box.y = rect->y + (rect->h - h) / 2;
    connect_box.w = w;
    connect_box.h = h;

    x = connect_box.x + DLG_PAD;
    y = connect_box.y + DLG_PAD + line_h + 8;
    field_w = w - DLG_PAD * 2 - 100;

    connect_server_field.x = x + 100; connect_server_field.y = y; connect_server_field.w = field_w; connect_server_field.h = line_h + 4;
    y += line_h + 10;
    connect_nick_field.x = x + 100; connect_nick_field.y = y; connect_nick_field.w = field_w; connect_nick_field.h = line_h + 4;
    y += line_h + 10;
    connect_team_field.x = x + 100; connect_team_field.y = y; connect_team_field.w = field_w; connect_team_field.h = line_h + 4;
    y += line_h + 14;

    connect_spectator_check.x = x; connect_spectator_check.y = y; connect_spectator_check.w = 14; connect_spectator_check.h = 14;
    y += line_h + 8;
    connect_password_field.x = x + 100; connect_password_field.y = y; connect_password_field.w = field_w; connect_password_field.h = line_h + 4;
    y += line_h + 16;

    connect_original_radio.x = x; connect_original_radio.y = y; connect_original_radio.w = 14; connect_original_radio.h = 14;
    connect_tetrifast_radio.x = x + 160; connect_tetrifast_radio.y = y; connect_tetrifast_radio.w = 14; connect_tetrifast_radio.h = 14;

    connect_ok_button.w = 80;
    connect_ok_button.h = line_h + 8;
    connect_ok_button.x = connect_box.x + w - DLG_PAD - 80;
    connect_ok_button.y = connect_box.y + h - DLG_PAD - connect_ok_button.h;
    connect_cancel_button = connect_ok_button;
    connect_cancel_button.x -= 90;
}

void
connectdialog_new (void)
{
    if (connectdialog_open)
        return;
    connectdialog_open = 1;
    connect_error[0] = 0;
    GTET_O_STRCPY (connect_server_buf, server);
    GTET_O_STRCPY (connect_nick_buf, nick);
    GTET_O_STRCPY (connect_team_buf, team);
    connect_password_buf[0] = 0;
    connect_spectator = spectating;
    connect_focus = CONNECT_FOCUS_SERVER;
}

static void
connectdialog_close (void)
{
    connectdialog_open = 0;
}

void
connectdialog_connected (void)
{
    connectdialog_close ();
}

static void
connectdialog_ok (void)
{
    gchar *nick_stripped;

    if (strlen (connect_server_buf) == 0) {
        GTET_O_STRCPY (connect_error, "You must specify a server name.");
        return;
    }

    spectating = connect_spectator;
    if (spectating) {
        if (strlen (connect_password_buf) == 0) {
            GTET_O_STRCPY (connect_error, "Please specify a password to connect as spectator.");
            return;
        }
        GTET_O_STRCPY (specpassword, connect_password_buf);
    }

    GTET_O_STRCPY (team, connect_team_buf);

    nick_stripped = g_strdup (connect_nick_buf);
    g_strstrip (nick_stripped);
    if (strlen (nick_stripped) == 0) {
        GTET_O_STRCPY (connect_error, "Please specify a valid nickname.");
        g_free (nick_stripped);
        return;
    }

    client_init (connect_server_buf, nick_stripped);

    config_set_server (connect_server_buf);
    config_set_nickname (nick_stripped);
    config_set_team (connect_team_buf);
    config_set_gamemode (gamemode);

    g_free (nick_stripped);

    connectdialog_close ();
}

static void
connectdialog_spectoggle (void)
{
    connect_spectator = !connect_spectator;
    /* Matches connectdialog_spectoggle()'s original sensitivity swap:
       team name is meaningless while spectating (no team to join), and
       the password is meaningless otherwise. If focus was sitting on
       the field that just became disabled, move it off. */
    if (connect_spectator && connect_focus == CONNECT_FOCUS_TEAM)
        connect_focus = CONNECT_FOCUS_PASSWORD;
    else if (!connect_spectator && connect_focus == CONNECT_FOCUS_PASSWORD)
        connect_focus = CONNECT_FOCUS_TEAM;
}

static void
connectdialog_render (SDL_Surface *dst, const SDL_Rect *rect)
{
    int line_h = misc_font_line_height ();

    connectdialog_layout (rect);
    draw_box (dst, &connect_box, "Connect to server");

    draw_text (dst, connect_box.x + DLG_PAD, connect_server_field.y + 2, "Server:", 0);
    draw_field (dst, &connect_server_field, connect_server_buf, connect_focus == CONNECT_FOCUS_SERVER, 1);

    draw_text (dst, connect_box.x + DLG_PAD, connect_nick_field.y + 2, "Nick name:", 0);
    draw_field (dst, &connect_nick_field, connect_nick_buf, connect_focus == CONNECT_FOCUS_NICK, 1);

    draw_text (dst, connect_box.x + DLG_PAD, connect_team_field.y + 2, "Team name:", 0);
    draw_field (dst, &connect_team_field, connect_team_buf, connect_focus == CONNECT_FOCUS_TEAM, !connect_spectator);

    draw_checkbox (dst, &connect_spectator_check, "Connect as a spectator", connect_spectator);

    draw_text (dst, connect_box.x + DLG_PAD, connect_password_field.y + 2, "Password:", 0);
    draw_field (dst, &connect_password_field, connect_password_buf, connect_focus == CONNECT_FOCUS_PASSWORD, connect_spectator);

    draw_radio (dst, &connect_original_radio, "Original", gamemode == ORIGINAL);
    draw_radio (dst, &connect_tetrifast_radio, "TetriFast", gamemode == TETRIFAST);

    if (connect_error[0])
        draw_text_error (dst, connect_box.x + DLG_PAD, connect_ok_button.y - line_h - 4, connect_error);

    draw_button (dst, &connect_cancel_button, "Cancel", 1);
    draw_button (dst, &connect_ok_button, "OK", 1);
}

static void
connectdialog_click (int x, int y)
{
    if (point_in_rect (x, y, &connect_ok_button)) { connectdialog_ok (); return; }
    if (point_in_rect (x, y, &connect_cancel_button)) { connectdialog_close (); return; }
    if (point_in_rect (x, y, &connect_server_field)) { connect_focus = CONNECT_FOCUS_SERVER; return; }
    if (point_in_rect (x, y, &connect_nick_field)) { connect_focus = CONNECT_FOCUS_NICK; return; }
    if (!connect_spectator && point_in_rect (x, y, &connect_team_field)) { connect_focus = CONNECT_FOCUS_TEAM; return; }
    if (connect_spectator && point_in_rect (x, y, &connect_password_field)) { connect_focus = CONNECT_FOCUS_PASSWORD; return; }
    if (point_in_rect (x, y, &connect_spectator_check)) { connectdialog_spectoggle (); return; }
    if (point_in_rect (x, y, &connect_original_radio)) { gamemode = ORIGINAL; return; }
    if (point_in_rect (x, y, &connect_tetrifast_radio)) { gamemode = TETRIFAST; return; }
}

static void
connectdialog_textinput (const char *text)
{
    int len;
    char *buf = connect_focus_buf (connect_focus, &len);
    size_t cap = (connect_focus == CONNECT_FOCUS_SERVER) ? sizeof (connect_server_buf)
               : (connect_focus == CONNECT_FOCUS_NICK)   ? sizeof (connect_nick_buf)
               : (connect_focus == CONNECT_FOCUS_TEAM)   ? sizeof (connect_team_buf)
               :                                            sizeof (connect_password_buf);
    size_t addlen = strlen (text);

    if ((size_t) len + addlen > cap - 1)
        addlen = cap - 1 - (size_t) len;
    if ((int) addlen <= 0)
        return;
    memcpy (buf + len, text, addlen);
    buf[len + (int) addlen] = 0;
}

static void
connectdialog_backspace (void)
{
    int len;
    char *buf = connect_focus_buf (connect_focus, &len);

    if (len == 0)
        return;
    len--;
    while (len > 0 && ((unsigned char) buf[len] & 0xC0) == 0x80)
        len--;
    buf[len] = 0;
}

static void
connectdialog_keydown (int keycode)
{
    if (keycode == SDLK_RETURN || keycode == SDLK_KP_ENTER) {
        connectdialog_ok ();
    }
    else if (keycode == SDLK_ESCAPE) {
        connectdialog_close ();
    }
    else if (keycode == SDLK_TAB) {
        switch (connect_focus) {
        case CONNECT_FOCUS_SERVER: connect_focus = CONNECT_FOCUS_NICK; break;
        case CONNECT_FOCUS_NICK:
            connect_focus = connect_spectator ? CONNECT_FOCUS_PASSWORD : CONNECT_FOCUS_TEAM;
            break;
        case CONNECT_FOCUS_TEAM:
        case CONNECT_FOCUS_PASSWORD:
            connect_focus = CONNECT_FOCUS_SERVER;
            break;
        }
    }
}

/*************************/
/* the change key dialog */
/*************************/

static int keydialog_open;
static int keydialog_keyidx;
static char keydialog_msg[256];
static SDL_Rect keydialog_box, keydialog_cancel_button;

static const char *key_action_names[K_NUM] = {
    "Move right", "Move left", "Rotate right", "Rotate left", "Move down",
    "Drop piece", "Discard special", "Send message",
    "Special to field 1", "Special to field 2", "Special to field 3",
    "Special to field 4", "Special to field 5", "Special to field 6",
};

static void
keydialog_layout (const SDL_Rect *rect)
{
    int line_h = misc_font_line_height ();
    int w = 320, h = 100;

    keydialog_box.x = rect->x + (rect->w - w) / 2;
    keydialog_box.y = rect->y + (rect->h - h) / 2;
    keydialog_box.w = w;
    keydialog_box.h = h;

    keydialog_cancel_button.w = 80;
    keydialog_cancel_button.h = line_h + 8;
    keydialog_cancel_button.x = keydialog_box.x + (w - 80) / 2;
    keydialog_cancel_button.y = keydialog_box.y + h - DLG_PAD - keydialog_cancel_button.h;
}

static void
keydialog_start (int keyidx)
{
    keydialog_open = 1;
    keydialog_keyidx = keyidx;
    g_snprintf (keydialog_msg, sizeof (keydialog_msg), "Press new key for \"%s\"", key_action_names[keyidx]);
}

static void
keydialog_render (SDL_Surface *dst, const SDL_Rect *rect)
{
    keydialog_layout (rect);
    draw_box (dst, &keydialog_box, "Change Key");
    draw_text (dst, keydialog_box.x + DLG_PAD, keydialog_box.y + DLG_PAD + misc_font_line_height () + 4, keydialog_msg, 0);
    draw_button (dst, &keydialog_cancel_button, "Cancel", 1);
}

static void
keydialog_click (int x, int y)
{
    if (point_in_rect (x, y, &keydialog_cancel_button))
        keydialog_open = 0;
}

static void
keydialog_keydown (int keycode)
{
    if (keycode == SDLK_ESCAPE) {
        keydialog_open = 0;
        return;
    }
    config_set_key (keydialog_keyidx, keycode);
    keydialog_open = 0;
}

/**************************/
/* the preferences dialog */
/**************************/

typedef enum {
    PREFS_TAB_THEMES,
    PREFS_TAB_PARTYLINE,
    PREFS_TAB_KEYBOARD,
    PREFS_TAB_SOUND,
    PREFS_TAB_COUNT
} T_prefs_tab;

static int prefdialog_open;
static T_prefs_tab prefs_tab;

struct themelistentry {
    char dir[1024];
    char name[1024];
};

#define MAX_THEMES 64
static struct themelistentry themes[MAX_THEMES];
static int themecount;
static int theme_select;
static char pref_theme_name[1024], pref_theme_author[1024], pref_theme_desc[1024];

static int prefs_key_selected;

static SDL_Rect prefs_box;
static SDL_Rect prefs_tab_rects[PREFS_TAB_COUNT];
static SDL_Rect prefs_close_button;
static SDL_Rect prefs_theme_rows[MAX_THEMES];
static SDL_Rect prefs_timestamp_check, prefs_channellist_check;
static SDL_Rect prefs_key_rows[K_NUM];
static SDL_Rect prefs_changekey_button, prefs_restorekeys_button;
static SDL_Rect prefs_sound_check;

static const char *prefs_tab_names[PREFS_TAB_COUNT] = {
    "Themes", "Partyline", "Keyboard", "Sound"
};

static int
themelistcomp (const void *a1, const void *b1)
{
    const struct themelistentry *a = a1, *b = b1;
    return strcmp (a->name, b->name);
}

static void
prefdialog_theme_select (int n)
{
    char author[1024], desc[1024];

    if (n < 0 || n >= themecount)
        return;
    theme_select = n;
    config_getthemeinfo (themes[n].dir, NULL, author, desc);
    GTET_STRCPY (pref_theme_name, themes[n].name, sizeof (pref_theme_name));
    GTET_STRCPY (pref_theme_author, author, sizeof (pref_theme_author));
    GTET_STRCPY (pref_theme_desc, desc, sizeof (pref_theme_desc));

    /* Matches the original: this only persists the choice for next
       launch (config_loadconfig_themes() is what actually calls
       load_theme()) -- switching themes doesn't re-skin the running
       game live, upstream never wired that up either. */
    config_set_theme_directory (themes[n].dir);
}

static void
prefdialog_themelist_load (void)
{
    DIR *d;
    struct dirent *de;
    char buf[1024];
    gchar *dir;
    int i;
    char *basedir[2];
    int selected = 0;

    dir = g_build_filename (getenv ("HOME"), ".gtetrinet", "themes", NULL);
    basedir[0] = dir;
    basedir[1] = GTETRINET_THEMES;

    themecount = 0;
    for (i = 0; i < 2; i++) {
        d = opendir (basedir[i]);
        if (!d)
            continue;
        while ((de = readdir (d))) {
            char str[1024];

            GTET_O_STRCPY (buf, basedir[i]);
            GTET_O_STRCAT (buf, "/");
            GTET_O_STRCAT (buf, de->d_name);
            GTET_O_STRCAT (buf, "/");

            if (config_getthemeinfo (buf, str, NULL, NULL) == 0) {
                GTET_O_STRCPY (themes[themecount].dir, buf);
                GTET_O_STRCPY (themes[themecount].name, str);
                themecount++;
                if (themecount == MAX_THEMES) {
                    closedir (d);
                    goto too_many_themes;
                }
            }
        }
        closedir (d);
    }
 too_many_themes:
    g_free (dir);
    qsort (themes, themecount, sizeof (struct themelistentry), themelistcomp);

    for (i = 0; i < themecount; i++)
        if (strcmp (themes[i].dir, currenttheme->str) == 0)
            selected = i;

    if (themecount > 0)
        prefdialog_theme_select (selected);
}

void
prefdialog_new (void)
{
    if (prefdialog_open)
        return;
    prefdialog_open = 1;
    prefs_tab = PREFS_TAB_THEMES;
    prefs_key_selected = 0;
    prefdialog_themelist_load ();
}

static void
prefdialog_close (void)
{
    prefdialog_open = 0;
    keydialog_open = 0;
}

static void
prefdialog_restorekeys (void)
{
    int i;

    for (i = 0; i < K_NUM; i++)
        config_set_key (i, defaultkeys[i]);
}

static void
prefdialog_layout (const SDL_Rect *rect)
{
    int line_h = misc_font_line_height ();
    int w = 480, h = 380;
    int i, x, y;

    prefs_box.x = rect->x + (rect->w - w) / 2;
    prefs_box.y = rect->y + (rect->h - h) / 2;
    prefs_box.w = w;
    prefs_box.h = h;

    x = prefs_box.x + DLG_PAD;
    y = prefs_box.y + DLG_PAD + line_h + 8;
    for (i = 0; i < PREFS_TAB_COUNT; i++) {
        prefs_tab_rects[i].x = x;
        prefs_tab_rects[i].y = y;
        prefs_tab_rects[i].w = 100;
        prefs_tab_rects[i].h = line_h + 6;
        x += 104;
    }

    y += line_h + 14;

    if (prefs_tab == PREFS_TAB_THEMES) {
        int row_y = y;
        for (i = 0; i < themecount && row_y + line_h <= prefs_box.y + h - 50; i++) {
            prefs_theme_rows[i].x = prefs_box.x + DLG_PAD;
            prefs_theme_rows[i].y = row_y;
            prefs_theme_rows[i].w = 160;
            prefs_theme_rows[i].h = line_h;
            row_y += line_h;
        }
    }
    else if (prefs_tab == PREFS_TAB_PARTYLINE) {
        prefs_timestamp_check.x = prefs_box.x + DLG_PAD;
        prefs_timestamp_check.y = y;
        prefs_timestamp_check.w = 14;
        prefs_timestamp_check.h = 14;

        prefs_channellist_check.x = prefs_box.x + DLG_PAD;
        prefs_channellist_check.y = y + line_h + 12;
        prefs_channellist_check.w = 14;
        prefs_channellist_check.h = 14;
    }
    else if (prefs_tab == PREFS_TAB_KEYBOARD) {
        int row_y = y;
        for (i = 0; i < K_NUM && row_y + line_h <= prefs_box.y + h - 50; i++) {
            prefs_key_rows[i].x = prefs_box.x + DLG_PAD;
            prefs_key_rows[i].y = row_y;
            prefs_key_rows[i].w = w - DLG_PAD * 2;
            prefs_key_rows[i].h = line_h;
            row_y += line_h;
        }
        prefs_changekey_button.w = 120;
        prefs_changekey_button.h = line_h + 8;
        prefs_changekey_button.x = prefs_box.x + DLG_PAD;
        prefs_changekey_button.y = prefs_box.y + h - DLG_PAD - 40 - prefs_changekey_button.h;
        prefs_restorekeys_button = prefs_changekey_button;
        prefs_restorekeys_button.x += 130;
    }
    else if (prefs_tab == PREFS_TAB_SOUND) {
        prefs_sound_check.x = prefs_box.x + DLG_PAD;
        prefs_sound_check.y = y;
        prefs_sound_check.w = 14;
        prefs_sound_check.h = 14;
    }

    prefs_close_button.w = 80;
    prefs_close_button.h = line_h + 8;
    prefs_close_button.x = prefs_box.x + w - DLG_PAD - 80;
    prefs_close_button.y = prefs_box.y + h - DLG_PAD - prefs_close_button.h;
}

static void
prefdialog_render (SDL_Surface *dst, const SDL_Rect *rect)
{
    int line_h = misc_font_line_height ();
    int i;

    prefdialog_layout (rect);
    draw_box (dst, &prefs_box, "GTetrinet Preferences");

    for (i = 0; i < PREFS_TAB_COUNT; i++) {
        int active = ((int) prefs_tab == i);
        fill_box (dst, &prefs_tab_rects[i], active ? 55 : 38, active ? 55 : 38, active ? 55 : 38);
        draw_border (dst, &prefs_tab_rects[i], 110, 110, 110);
        draw_text (dst, prefs_tab_rects[i].x + 6, prefs_tab_rects[i].y + 3, prefs_tab_names[i], active);
    }

    if (prefs_tab == PREFS_TAB_THEMES) {
        for (i = 0; i < themecount && i < MAX_THEMES; i++) {
            if (prefs_theme_rows[i].w == 0)
                break;
            if (i == theme_select)
                fill_box (dst, &prefs_theme_rows[i], 50, 50, 70);
            draw_text (dst, prefs_theme_rows[i].x + 2, prefs_theme_rows[i].y, themes[i].name, 0);
        }
        {
            int detail_x = prefs_box.x + DLG_PAD + 176;
            int detail_y = prefs_theme_rows[0].y;
            draw_text (dst, detail_x, detail_y, "Name:", 0);
            draw_text (dst, detail_x + 90, detail_y, pref_theme_name, 0);
            draw_text (dst, detail_x, detail_y + line_h + 4, "Author:", 0);
            draw_text (dst, detail_x + 90, detail_y + line_h + 4, pref_theme_author, 0);
            draw_text (dst, detail_x, detail_y + 2 * (line_h + 4), "Description:", 0);
            draw_text (dst, detail_x + 90, detail_y + 2 * (line_h + 4), pref_theme_desc, 0);
        }
    }
    else if (prefs_tab == PREFS_TAB_PARTYLINE) {
        draw_checkbox (dst, &prefs_timestamp_check, "Enable Timestamps", timestampsenable);
        draw_checkbox (dst, &prefs_channellist_check, "Enable Channel List", list_enabled);
    }
    else if (prefs_tab == PREFS_TAB_KEYBOARD) {
        for (i = 0; i < K_NUM; i++) {
            char buf[300];
            const char *keyname = SDL_GetKeyName ((SDL_Keycode) keys[i]);

            if (prefs_key_rows[i].w == 0)
                break;
            if (i == prefs_key_selected)
                fill_box (dst, &prefs_key_rows[i], 50, 50, 70);
            g_snprintf (buf, sizeof (buf), "%s: %s", key_action_names[i], keyname ? keyname : "?");
            draw_text (dst, prefs_key_rows[i].x + 2, prefs_key_rows[i].y, buf, 0);
        }
        draw_button (dst, &prefs_changekey_button, "Change key...", 1);
        draw_button (dst, &prefs_restorekeys_button, "Restore defaults", 1);
    }
    else if (prefs_tab == PREFS_TAB_SOUND) {
        draw_checkbox (dst, &prefs_sound_check, "Enable Sound", soundenable);
    }

    draw_button (dst, &prefs_close_button, "Close", 1);
}

static void
prefdialog_click (int x, int y)
{
    int i;

    if (point_in_rect (x, y, &prefs_close_button)) { prefdialog_close (); return; }

    for (i = 0; i < PREFS_TAB_COUNT; i++)
        if (point_in_rect (x, y, &prefs_tab_rects[i])) { prefs_tab = (T_prefs_tab) i; return; }

    if (prefs_tab == PREFS_TAB_THEMES) {
        for (i = 0; i < themecount && i < MAX_THEMES; i++)
            if (prefs_theme_rows[i].w != 0 && point_in_rect (x, y, &prefs_theme_rows[i])) {
                prefdialog_theme_select (i);
                return;
            }
    }
    else if (prefs_tab == PREFS_TAB_PARTYLINE) {
        if (point_in_rect (x, y, &prefs_timestamp_check)) {
            timestampsenable = !timestampsenable;
            config_set_timestamps_enable (timestampsenable);
            return;
        }
        if (point_in_rect (x, y, &prefs_channellist_check)) {
            partyline_show_channel_list (!list_enabled);
            config_set_channel_list_enable (list_enabled);
            return;
        }
    }
    else if (prefs_tab == PREFS_TAB_KEYBOARD) {
        for (i = 0; i < K_NUM; i++)
            if (prefs_key_rows[i].w != 0 && point_in_rect (x, y, &prefs_key_rows[i])) {
                prefs_key_selected = i;
                return;
            }
        if (point_in_rect (x, y, &prefs_changekey_button)) { keydialog_start (prefs_key_selected); return; }
        if (point_in_rect (x, y, &prefs_restorekeys_button)) { prefdialog_restorekeys (); return; }
    }
    else if (prefs_tab == PREFS_TAB_SOUND) {
        if (point_in_rect (x, y, &prefs_sound_check)) {
            soundenable = !soundenable;
            config_set_sound_enable (soundenable);
            return;
        }
    }
}

static void
prefdialog_keydown (int keycode)
{
    if (keycode == SDLK_ESCAPE)
        prefdialog_close ();
}

/*****************/
/* about dialog  */
/*****************/

static int aboutdialog_open;
static SDL_Rect about_box, about_ok_button;

void
aboutdialog_new (void)
{
    aboutdialog_open = 1;
}

static void
aboutdialog_close (void)
{
    aboutdialog_open = 0;
}

static void
aboutdialog_layout (const SDL_Rect *rect)
{
    int line_h = misc_font_line_height ();
    int w = 360, h = 240;

    about_box.x = rect->x + (rect->w - w) / 2;
    about_box.y = rect->y + (rect->h - h) / 2;
    about_box.w = w;
    about_box.h = h;

    about_ok_button.w = 80;
    about_ok_button.h = line_h + 8;
    about_ok_button.x = about_box.x + (w - 80) / 2;
    about_ok_button.y = about_box.y + h - DLG_PAD - about_ok_button.h;
}

static void
aboutdialog_render (SDL_Surface *dst, const SDL_Rect *rect)
{
    int line_h = misc_font_line_height ();
    int y;

    aboutdialog_layout (rect);
    draw_box (dst, &about_box, "About GTetrinet");

    y = about_box.y + DLG_PAD + line_h + 10;
    draw_text (dst, about_box.x + DLG_PAD, y, "GTetrinet " VERSION, 1); y += line_h + 4;
    draw_text_muted (dst, about_box.x + DLG_PAD, y, "A Tetrinet client for GNOME."); y += line_h + 10;
    draw_text_muted (dst, about_box.x + DLG_PAD, y, "Copyright 2004, 2005 Jordi Mallach, Dani Carbonell"); y += line_h + 2;
    draw_text_muted (dst, about_box.x + DLG_PAD, y, "Copyright 1999-2003 Ka-shu Wong"); y += line_h + 10;
    draw_text_muted (dst, about_box.x + DLG_PAD, y, "http://gtetrinet.sf.net");

    draw_button (dst, &about_ok_button, "OK", 1);
}

static void
aboutdialog_click (int x, int y)
{
    if (point_in_rect (x, y, &about_ok_button))
        aboutdialog_close ();
}

static void
aboutdialog_keydown (int keycode)
{
    if (keycode == SDLK_RETURN || keycode == SDLK_KP_ENTER || keycode == SDLK_ESCAPE)
        aboutdialog_close ();
}

/* --- public API -------------------------------------------------------
 * Priority when more than one flag is set (topmost first): the
 * key-capture modal (nested inside Preferences), then the connecting-
 * progress overlay (which can legitimately be open together with the
 * connect dialog -- see dialogs.h), then whichever single main dialog
 * is open. */

int
dialog_is_open (void)
{
    return connectdialog_open || connectingdialog_open || team_dialog_open ||
           prefdialog_open || aboutdialog_open || keydialog_open;
}

void
dialog_render (SDL_Surface *dst, const SDL_Rect *rect)
{
    if (!dialog_is_open ())
        return;

    fill_box (dst, rect, 10, 10, 10);

    if (connectdialog_open)
        connectdialog_render (dst, rect);
    else if (team_dialog_open)
        teamdialog_render (dst, rect);
    else if (prefdialog_open)
        prefdialog_render (dst, rect);
    else if (aboutdialog_open)
        aboutdialog_render (dst, rect);

    if (connectingdialog_open)
        connectingdialog_render (dst, rect);

    if (keydialog_open)
        keydialog_render (dst, rect);
}

void
dialog_textinput (const char *text)
{
    if (keydialog_open || connectingdialog_open)
        return;
    if (connectdialog_open)
        connectdialog_textinput (text);
    else if (team_dialog_open)
        teamdialog_textinput (text);
}

void
dialog_backspace (void)
{
    if (keydialog_open || connectingdialog_open)
        return;
    if (connectdialog_open)
        connectdialog_backspace ();
    else if (team_dialog_open)
        teamdialog_backspace ();
}

void
dialog_keydown (int keycode)
{
    if (keydialog_open) { keydialog_keydown (keycode); return; }
    if (connectingdialog_open) return; /* only Cancel, mouse-only */
    if (connectdialog_open) { connectdialog_keydown (keycode); return; }
    if (team_dialog_open) { teamdialog_keydown (keycode); return; }
    if (prefdialog_open) { prefdialog_keydown (keycode); return; }
    if (aboutdialog_open) { aboutdialog_keydown (keycode); return; }
}

void
dialog_click (int x, int y)
{
    if (keydialog_open) { keydialog_click (x, y); return; }
    if (connectingdialog_open) { connectingdialog_click (x, y); return; }
    if (connectdialog_open) { connectdialog_click (x, y); return; }
    if (team_dialog_open) { teamdialog_click (x, y); return; }
    if (prefdialog_open) { prefdialog_click (x, y); return; }
    if (aboutdialog_open) { aboutdialog_click (x, y); return; }
}

void
dialog_tick (void)
{
    connectingdialog_tick ();
}
