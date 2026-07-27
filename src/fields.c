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

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <glib.h>
#include <SDL_image.h>

#include "gtet_config.h"
#include "client.h"
#include "tetrinet.h"
#include "tetris.h"
#include "fields.h"
#include "misc.h"

#define BLOCKSIZE bsize
#define SMALLBLOCKSIZE (BLOCKSIZE/2)
#define MARGIN 8
#define NUM_OPPONENT_FIELDS 5
#define SPECIALS_SLOTS 18
#define GMSG_INPUT_MAXLEN 511
#define LOG_VISIBLE_LINES 6

static SDL_Surface *blockpix;
/* Set once blockpix has been converted (in fields_render(), the first
 * call after each load) to match the destination surface's own pixel
 * format -- reset to 0 whenever a new blockpix is loaded. Without this,
 * SDL_BlitSurface() has to do a per-pixel format conversion on every
 * blit_tile() call whenever the theme PNG's format doesn't happen to
 * match dst's, and there are 12*22*6 = 1584 of those calls every single
 * frame (render_field() for the own field plus 5 opponents) -- on
 * hardware with no blit acceleration (real IRIX/O2) that overhead is
 * the difference between playable and not. */
static int blockpix_converted;

static FIELD displayfields[6]; /* what is actually displayed; field 0 == own */
static TETRISBLOCK displayblock;

static char fieldname[6][128];
static char fieldteam[6][128];
static int fieldnum[6];
static int fieldhasname[6]; /* 0 -> show "Not playing" placeholder */

static char speciallabeltext[128] = "Specials:";

static char linestext[16] = "";
static char leveltext[16] = "";
static int activelevel_visible = 0;
static char activeleveltext[16] = "";

/* T_textlog (attdeflog/gmsglog's type) is misc.h's shared scrolling
 * formatted-text-log component -- see misc.h for why this was pulled
 * out of fields.c once partyline.c needed the exact same thing. */
static T_textlog attdeflog;
static T_textlog gmsglog;

static int gmsginput_visible;
static char gmsginput_buf[GMSG_INPUT_MAXLEN + 1];
static int gmsginput_len;

/* Layout, computed once per fields_page_new() call (theme/BLOCKSIZE may
 * have changed since the last call). */
static SDL_Rect ownfield_rect;
static SDL_Rect opponentfield_rect[NUM_OPPONENT_FIELDS];
static SDL_Rect nextpiece_rect;
static SDL_Rect specials_rect;
static SDL_Rect labels_rect;   /* lines/level/activelevel text area */
static SDL_Rect attdef_rect;
static SDL_Rect gmsg_rect;
static SDL_Rect gmsginput_rect;
static int screen_w, screen_h;

static void fields_setup_layout (void);
static void render_field (SDL_Surface *dst, int field, const SDL_Rect *rect);
static void render_nextblock (SDL_Surface *dst);
static void render_specials (SDL_Surface *dst);
static void render_labels (SDL_Surface *dst);
static void render_gmsginput (SDL_Surface *dst);

static void
blit_tile (SDL_Surface *dst, int srcx, int srcy, int destx, int desty, int w, int h)
{
    SDL_Rect srcrect, dstrect;
    srcrect.x = srcx; srcrect.y = srcy; srcrect.w = w; srcrect.h = h;
    dstrect.x = destx; dstrect.y = desty; dstrect.w = w; dstrect.h = h;
    SDL_BlitSurface (blockpix, &srcrect, dst, &dstrect);
}

int fields_init (void)
{
    blockpix = IMG_Load (blocksfile);
    if (blockpix == NULL) {
        fprintf (stderr, "Error loading theme graphics '%s': %s\n", blocksfile, SDL_GetError ());
        fprintf (stderr, "Falling back to default theme\n");
        g_string_assign (currenttheme, DEFAULTTHEME);
        config_loadtheme (DEFAULTTHEME);
        blockpix = IMG_Load (blocksfile);
        if (blockpix == NULL) {
            /* shouldn't happen -- installation error */
            fprintf (stderr, "Error loading default theme: aborting. Check for installation errors.\n");
            return -1;
        }
    }
    blockpix_converted = 0;
    return 0;
}

void fields_cleanup (void)
{
    if (blockpix) {
        SDL_FreeSurface (blockpix);
        blockpix = NULL;
    }
}

void fields_page_new (void)
{
    int i;

    for (i = 0; i < 6; i++)
        fields_setlabel (i, NULL, NULL, 0);

    fields_setup_layout ();

    fields_setlines (-1);
    fields_setlevel (-1);
    fields_setactivelevel (-1);
    fields_gmsginput (0);
}

void fields_page_destroy_contents (void)
{
    /* No widget tree to tear down in the SDL port -- state persists
     * across page switches (matches the underlying game state's own
     * lifetime, which was never tied to GTK widget lifetime either). */
}

static void
fields_setup_layout (void)
{
    int ownfield_w = BLOCKSIZE * FIELDWIDTH;
    int ownfield_h = BLOCKSIZE * FIELDHEIGHT;
    int smallfield_w = SMALLBLOCKSIZE * FIELDWIDTH;
    int smallfield_h = SMALLBLOCKSIZE * FIELDHEIGHT;
    int nextpiece_size = BLOCKSIZE * 9 / 2;
    int specials_w = SPECIALS_SLOTS * BLOCKSIZE;
    int right_col_x = MARGIN + ownfield_w + MARGIN;
    int right_col_w;
    int i;
    int loglines_h;

    ownfield_rect.x = MARGIN;
    ownfield_rect.y = MARGIN;
    ownfield_rect.w = ownfield_w;
    ownfield_rect.h = ownfield_h;

    /* 5 opponent fields in a row above the next-piece/specials/labels area */
    for (i = 0; i < NUM_OPPONENT_FIELDS; i++) {
        opponentfield_rect[i].x = right_col_x + i * (smallfield_w + MARGIN);
        opponentfield_rect[i].y = MARGIN;
        opponentfield_rect[i].w = smallfield_w;
        opponentfield_rect[i].h = smallfield_h;
    }
    right_col_w = NUM_OPPONENT_FIELDS * smallfield_w + (NUM_OPPONENT_FIELDS - 1) * MARGIN;
    if (right_col_w < specials_w)
        right_col_w = specials_w;

    /* render_fieldlabel() draws opponent field 1's name ("Not playing"
       until it joins) at rect->y + rect->h + 2, directly below the
       opponent-field row -- a bare MARGIN gap here isn't tall enough
       to clear a full text line, so it collided with the next-piece
       box/Lines:/Level: labels starting right at this y. */
    nextpiece_rect.x = right_col_x;
    nextpiece_rect.y = MARGIN + smallfield_h + MARGIN + misc_font_line_height ();
    nextpiece_rect.w = nextpiece_size;
    nextpiece_rect.h = nextpiece_size;

    labels_rect.x = right_col_x + nextpiece_size + MARGIN;
    labels_rect.y = nextpiece_rect.y;
    labels_rect.w = right_col_w - nextpiece_size - MARGIN;
    labels_rect.h = nextpiece_size;

    specials_rect.x = right_col_x;
    specials_rect.y = nextpiece_rect.y + nextpiece_size + MARGIN;
    specials_rect.w = specials_w;
    specials_rect.h = BLOCKSIZE;

    loglines_h = LOG_VISIBLE_LINES * misc_font_line_height () + 2 * MARGIN;

    attdef_rect.x = right_col_x;
    attdef_rect.y = specials_rect.y + specials_rect.h + MARGIN;
    attdef_rect.w = right_col_w;
    attdef_rect.h = loglines_h;

    gmsg_rect.x = right_col_x;
    gmsg_rect.y = attdef_rect.y + attdef_rect.h + MARGIN;
    gmsg_rect.w = right_col_w;
    gmsg_rect.h = loglines_h;

    gmsginput_rect.x = right_col_x;
    gmsginput_rect.y = gmsg_rect.y + gmsg_rect.h + MARGIN;
    gmsginput_rect.w = right_col_w;
    gmsginput_rect.h = misc_font_line_height () + 2 * MARGIN / 2;

    screen_w = right_col_x + right_col_w + MARGIN;
    screen_h = ownfield_h + MARGIN * 2;
    if (gmsginput_rect.y + gmsginput_rect.h + MARGIN > screen_h)
        screen_h = gmsginput_rect.y + gmsginput_rect.h + MARGIN;
}

int fields_screen_width (void)  { return screen_w; }
int fields_screen_height (void) { return screen_h; }

void fields_drawfield (int field, FIELD newfield)
{
    memcpy (displayfields[field], newfield, sizeof (FIELD));
}

static void
render_field (SDL_Surface *dst, int field, const SDL_Rect *rect)
{
    int x, y, srcx, srcy, destx, desty, blocksize;
    char block;

    blocksize = (field == 0) ? BLOCKSIZE : SMALLBLOCKSIZE;

    for (y = 0; y < FIELDHEIGHT; y++) {
        for (x = 0; x < FIELDWIDTH; x++) {
            block = displayfields[field][y][x];

            if (field == 0) {
                if (block == 0) {
                    srcx = blocksize * x;
                    srcy = BLOCKSIZE + SMALLBLOCKSIZE + blocksize * y;
                } else {
                    srcx = (block - 1) * blocksize;
                    srcy = 0;
                }
            } else {
                if (block == 0) {
                    srcx = BLOCKSIZE * FIELDWIDTH + blocksize * x;
                    srcy = BLOCKSIZE + SMALLBLOCKSIZE + blocksize * y;
                } else {
                    srcx = (block - 1) * blocksize;
                    srcy = BLOCKSIZE;
                }
            }
            destx = rect->x + blocksize * x;
            desty = rect->y + blocksize * y;

            blit_tile (dst, srcx, srcy, destx, desty, blocksize, blocksize);
        }
    }
}

void fields_setlabel (int field, char *name, char *team, int num)
{
    if (name == NULL) {
        fieldhasname[field] = 0;
        fieldname[field][0] = 0;
        fieldteam[field][0] = 0;
        fieldnum[field] = 0;
    } else {
        fieldhasname[field] = 1;
        GTET_O_STRCPY (fieldname[field], name);
        fieldteam[field][0] = 0;
        if (team != NULL && team[0] != 0)
            GTET_O_STRCPY (fieldteam[field], team);
        fieldnum[field] = num;
    }
}

void fields_setspeciallabel (char *label)
{
    if (label == NULL)
        GTET_O_STRCPY (speciallabeltext, "Specials:");
    else
        GTET_O_STRCPY (speciallabeltext, label);
}

void fields_drawspecials (void)
{
    /* Nothing to precompute -- render_specials() reads specialblocks[]/
     * specialblocknum (tetrinet.c globals) directly every frame. */
}

static void
render_specials (SDL_Surface *dst)
{
    int i;
    SDL_Rect r;

    for (i = 0; i < SPECIALS_SLOTS; i++) {
        if (i < specialblocknum) {
            blit_tile (dst, (specialblocks[i] - 1) * BLOCKSIZE, 0,
                       specials_rect.x + BLOCKSIZE * i, specials_rect.y,
                       BLOCKSIZE, BLOCKSIZE);
        } else {
            r.x = specials_rect.x + BLOCKSIZE * i;
            r.y = specials_rect.y;
            r.w = BLOCKSIZE;
            r.h = BLOCKSIZE;
            SDL_FillRect (dst, &r, SDL_MapRGB (dst->format, 0, 0, 0));
        }
    }
}

void fields_drawnextblock (TETRISBLOCK block)
{
    if (block == NULL)
        return; /* NULL meant "redraw from displayblock" in the GTK build's
                    expose-event path; render_nextblock() always redraws
                    from displayblock now, so there's nothing to do. */
    memcpy (displayblock, block, sizeof (TETRISBLOCK));
}

static void
render_nextblock (SDL_Surface *dst)
{
    int x, y, xstart = 4, ystart = 4;

    SDL_FillRect (dst, &nextpiece_rect, SDL_MapRGB (dst->format, 0, 0, 0));

    for (y = 0; y < 4; y++)
        for (x = 0; x < 4; x++)
            if (displayblock[y][x]) {
                if (y < ystart) ystart = y;
                if (x < xstart) xstart = x;
            }
    if (ystart == 4 || xstart == 4)
        return; /* no block set yet */

    for (y = ystart; y < 4; y++)
        for (x = xstart; x < 4; x++)
            if (displayblock[y][x])
                blit_tile (dst, (displayblock[y][x] - 1) * BLOCKSIZE, 0,
                           nextpiece_rect.x + BLOCKSIZE * (x - xstart) + BLOCKSIZE / 4,
                           nextpiece_rect.y + BLOCKSIZE * (y - ystart) + BLOCKSIZE / 4,
                           BLOCKSIZE, BLOCKSIZE);
}

void fields_attdefmsg (char *text)
{
    misc_textlog_append (&attdeflog, text);
}

void fields_attdeffmt (const char *fmt, ...)
{
    va_list ap;
    char *text;

    va_start (ap, fmt);
    text = g_strdup_vprintf (fmt, ap);
    va_end (ap);

    fields_attdefmsg (text);
    g_free (text);
}

void fields_attdefclear (void)
{
    misc_textlog_clear (&attdeflog);
}

void fields_setlines (int l)
{
    linestext[0] = 0;
    if (l >= 0)
        g_snprintf (linestext, sizeof (linestext), "%d", l);
}

void fields_setlevel (int l)
{
    leveltext[0] = 0;
    if (l > 0)
        g_snprintf (leveltext, sizeof (leveltext), "%d", l);
}

void fields_setactivelevel (int l)
{
    if (l <= 0) {
        activelevel_visible = 0;
    } else {
        activelevel_visible = 1;
        g_snprintf (activeleveltext, sizeof (activeleveltext), "%d", l);
    }
}

static void
render_labels (SDL_Surface *dst)
{
    T_textstyle style;
    int y = labels_rect.y;
    int line_h = misc_font_line_height ();
    char buf[160];

    style.color.r = style.color.g = style.color.b = 0xFF;
    style.bold = style.italic = style.underline = 0;

    g_snprintf (buf, sizeof (buf), "Lines: %s", linestext);
    misc_font_render (dst, labels_rect.x, y, &style, buf, strlen (buf));
    y += line_h;

    g_snprintf (buf, sizeof (buf), "Level: %s", leveltext);
    misc_font_render (dst, labels_rect.x, y, &style, buf, strlen (buf));
    y += line_h;

    if (activelevel_visible) {
        g_snprintf (buf, sizeof (buf), "Active level: %s", activeleveltext);
        misc_font_render (dst, labels_rect.x, y, &style, buf, strlen (buf));
        y += line_h;
    }
}

static void
render_fieldlabel (SDL_Surface *dst, int field, const SDL_Rect *rect)
{
    T_textstyle style;
    char buf[160];

    style.color.r = style.color.g = style.color.b = 0xFF;
    style.bold = style.italic = style.underline = 0;

    if (!fieldhasname[field]) {
        misc_font_render (dst, rect->x, rect->y + rect->h + 2, &style, "Not playing", 11);
        return;
    }

    if (fieldteam[field][0])
        g_snprintf (buf, sizeof (buf), "%d: %s (%s)", fieldnum[field], fieldname[field], fieldteam[field]);
    else
        g_snprintf (buf, sizeof (buf), "%d: %s", fieldnum[field], fieldname[field]);
    misc_font_render (dst, rect->x, rect->y + rect->h + 2, &style, buf, strlen (buf));
}

void fields_gmsgadd (const char *str)
{
    misc_textlog_append (&gmsglog, str);
}

void fields_gmsgclear (void)
{
    misc_textlog_clear (&gmsglog);
}

void fields_gmsginput (int i)
{
    gmsginput_visible = i ? 1 : 0;
}

void fields_gmsginputclear (void)
{
    gmsginput_buf[0] = 0;
    gmsginput_len = 0;
}

void fields_gmsginputactivate (int t)
{
    if (t)
        fields_gmsginputclear ();
    /* else: do nothing, matching the GTK build */
}

void fields_gmsg_textinput (const char *text)
{
    size_t addlen;

    if (!gmsginput_visible || text == NULL)
        return;
    addlen = strlen (text);
    if (gmsginput_len + addlen > GMSG_INPUT_MAXLEN)
        addlen = GMSG_INPUT_MAXLEN - gmsginput_len;
    if ((int) addlen <= 0)
        return;
    memcpy (gmsginput_buf + gmsginput_len, text, addlen);
    gmsginput_len += (int) addlen;
    gmsginput_buf[gmsginput_len] = 0;
}

void fields_gmsg_backspace (void)
{
    if (!gmsginput_visible || gmsginput_len == 0)
        return;
    /* Step back one UTF-8 code point, not just one byte, so backspacing
     * a multi-byte character removes the whole character in one press
     * (continuation bytes are 10xxxxxx, i.e. (b & 0xC0) == 0x80). */
    gmsginput_len--;
    while (gmsginput_len > 0 && ((unsigned char) gmsginput_buf[gmsginput_len] & 0xC0) == 0x80)
        gmsginput_len--;
    gmsginput_buf[gmsginput_len] = 0;
}

static void
render_gmsginput (SDL_Surface *dst)
{
    T_textstyle style;
    SDL_Rect r = gmsginput_rect;

    if (!gmsginput_visible)
        return;

    SDL_FillRect (dst, &r, SDL_MapRGB (dst->format, 32, 32, 32));

    style.color.r = style.color.g = style.color.b = 0xFF;
    style.bold = style.italic = style.underline = 0;
    misc_font_render (dst, r.x + 2, r.y + 2, &style, gmsginput_buf, (size_t) gmsginput_len);
}

const char *fields_gmsginputtext (void)
{
    return gmsginput_buf;
}

void fields_render (SDL_Surface *dst)
{
    int i;

    if (!blockpix_converted) {
        SDL_Surface *converted = SDL_ConvertSurface (blockpix, dst->format, 0);
        if (converted) {
            SDL_FreeSurface (blockpix);
            blockpix = converted;
        }
        /* Set even on failure (converted == NULL, e.g. out of memory) --
           blit_tile() falls back to the slower mismatched-format path
           rather than retrying this conversion every single frame. */
        blockpix_converted = 1;
    }

    render_field (dst, 0, &ownfield_rect);
    render_fieldlabel (dst, 0, &ownfield_rect);
    for (i = 0; i < NUM_OPPONENT_FIELDS; i++) {
        render_field (dst, i + 1, &opponentfield_rect[i]);
        render_fieldlabel (dst, i + 1, &opponentfield_rect[i]);
    }

    render_nextblock (dst);
    render_specials (dst);
    render_labels (dst);

    misc_textlog_render (dst, &attdef_rect, &attdeflog);
    misc_textlog_render (dst, &gmsg_rect, &gmsglog);
    render_gmsginput (dst);
}
