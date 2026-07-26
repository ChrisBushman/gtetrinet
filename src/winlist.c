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
#include <SDL_image.h>

#include "client.h"
#include "tetrinet.h"
#include "winlist.h"
#include "misc.h"

#define WINLIST_MAX_ITEMS 64
#define ICON_SIZE 48 /* native size of icons/team.png, icons/alone.png --
                         rendered as-is rather than runtime-scaled to 24x24
                         like the GTK build did, to avoid needing an image
                         scaling routine of our own; a cosmetic difference
                         only. */

typedef struct {
    int team;
    char name[128];
    int score;
} T_winitem;

static SDL_Surface *team_icon, *alone_icon;
static T_winitem items[WINLIST_MAX_ITEMS];
static int item_count;

int winlist_page_new (void)
{
    team_icon = IMG_Load (GTETPIXMAPSDIR "/team.png");
    alone_icon = IMG_Load (GTETPIXMAPSDIR "/alone.png");
    if (team_icon == NULL || alone_icon == NULL) {
        fprintf (stderr, "winlist_page_new: failed to load team/alone icons: %s\n", SDL_GetError ());
        return -1;
    }
    item_count = 0;
    return 0;
}

void winlist_page_cleanup (void)
{
    if (team_icon) { SDL_FreeSurface (team_icon); team_icon = NULL; }
    if (alone_icon) { SDL_FreeSurface (alone_icon); alone_icon = NULL; }
}

void winlist_clear (void)
{
    item_count = 0;
}

void winlist_additem (int team, char *name, int score)
{
    T_winitem *it;

    if (item_count >= WINLIST_MAX_ITEMS)
        return; /* matches the original having no explicit cap either,
                    but an SDL fixed-size array needs one; a game only
                    ever has up to 6 players, so this is generous
                    headroom, not a real limit in practice. */
    it = &items[item_count++];
    it->team = team;
    GTET_STRCPY (it->name, nocolor (name), sizeof (it->name));
    it->score = score;
}

void winlist_focus (void)
{
    /* No keyboard-focus concept to hand off to in the SDL port -- the
     * main loop reads input directly rather than routing it through a
     * focused widget. Kept as a no-op so callers (tetrinet.c) don't need
     * a special case. */
}

void winlist_render (SDL_Surface *dst, const SDL_Rect *rect)
{
    T_textstyle style;
    int i;
    int row_h = ICON_SIZE + 4;
    int y = rect->y;
    char scorebuf[32];

    style.color.r = style.color.g = style.color.b = 0xFF;
    style.bold = style.italic = style.underline = 0;

    for (i = 0; i < item_count && y + row_h <= rect->y + rect->h; i++) {
        SDL_Surface *icon = items[i].team ? team_icon : alone_icon;
        SDL_Rect dstrect;
        int textx = rect->x;

        if (icon) {
            dstrect.x = rect->x;
            dstrect.y = y;
            dstrect.w = icon->w;
            dstrect.h = icon->h;
            SDL_BlitSurface (icon, NULL, dst, &dstrect);
            textx = rect->x + ICON_SIZE + 8;
        }

        textx += misc_font_render (dst, textx, y + row_h / 2 - misc_font_line_height () / 2,
                                    &style, items[i].name, strlen (items[i].name));
        g_snprintf (scorebuf, sizeof (scorebuf), "  %d", items[i].score);
        misc_font_render (dst, textx, y + row_h / 2 - misc_font_line_height () / 2,
                           &style, scorebuf, strlen (scorebuf));

        y += row_h;
    }
}
