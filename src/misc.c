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

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <SDL_ttf.h>

#include "misc.h"

/* returns a random number in the range 0 to n-1 --
 * Note both n==0 and n==1 always return 0 */
int randomnum (int n)
{
    return (float)n*rand()/(RAND_MAX+1.0);
}

void fdreadline (int fd, char *buf)
{
    int c = 0;
    /* read a single line - no overflow check */
    while (1) {
        if (read (fd, &buf[c], 1) == 0) break;
        if (buf[c] == '\n') break;
        c ++;
    }
    buf[c] = 0;
}

#define COLORNUM 26

/* Same 26-entry palette as the original GTK build's gtet_text_tags[],
 * just stored as SDL_Color (8-bit) instead of GdkColor (16-bit) --
 * values converted by >> 8, not re-picked, so this renders identically
 * to the GTK build. Index is the raw TETRI_TB_C_* control-code value
 * minus TETRI_TB_C_BEG_OFFSET, exactly as misc_parse_formatted() below
 * computes it (mirroring the original textbox_addtext()). */
static const SDL_Color gtet_colors[COLORNUM] = {
    {0x00, 0x00, 0x00, 0xFF}, /* ^A black */
    {0x00, 0x00, 0x00, 0xFF}, /* ^B black */
    {0x00, 0xFF, 0xFF, 0xFF}, /* ^C cyan */
    {0x00, 0x00, 0x00, 0xFF}, /* ^D black */
    {0x00, 0x00, 0xFF, 0xFF}, /* ^E bright blue */
    {0x7F, 0x7F, 0x7F, 0xFF}, /* ^F grey */
    {0x00, 0x00, 0x00, 0xFF}, /* ^G black */
    {0xFF, 0x00, 0xFF, 0xFF}, /* ^H magenta */
    {0x00, 0x00, 0x00, 0xFF}, /* ^I black */
    {0x00, 0x00, 0x00, 0xFF}, /* ^J black */
    {0x7F, 0x7F, 0x7F, 0xFF}, /* ^K grey */
    {0x00, 0x7F, 0x00, 0xFF}, /* ^L dark green */
    {0x00, 0x00, 0x00, 0xFF}, /* ^M black */
    {0x00, 0xFF, 0x00, 0xFF}, /* ^N bright green */
    {0xBF, 0xBF, 0xBF, 0xFF}, /* ^O light grey */
    {0x7F, 0x00, 0x00, 0xFF}, /* ^P dark red */
    {0x00, 0x00, 0x7F, 0xFF}, /* ^Q dark blue */
    {0x7F, 0x7F, 0x00, 0xFF}, /* ^R brown */
    {0x7F, 0x00, 0x7F, 0xFF}, /* ^S purple */
    {0xFF, 0x00, 0x00, 0xFF}, /* ^T bright red */
    {0xBF, 0xBF, 0xBF, 0xFF}, /* ^U light grey */
    {0x00, 0x00, 0x00, 0xFF}, /* ^V black */
    {0x00, 0x7F, 0x7F, 0xFF}, /* ^W dark cyan */
    {0xFF, 0xFF, 0xFF, 0xFF}, /* ^X white */
    {0xFF, 0xFF, 0x00, 0xFF}, /* ^Y yellow */
    {0x00, 0x00, 0x00, 0xFF}  /* ^Z black */
};

void misc_parse_formatted (const char *str, T_formattedrun_fn emit, void *userdata)
{
    T_textstyle style;
    T_textstyle laststyle; /* style to restore on a "repeat same color code" toggle-off */
    const char *run_start;
    unsigned char last_color_code = 0;
    const char *p;

    style.color = gtet_colors[0];
    style.bold = style.italic = style.underline = 0;
    laststyle = style;

    run_start = str;

    for (p = str; ; p++) {
        unsigned char c = (unsigned char) *p;
        int is_control = (c == TETRI_TB_RESET) || (c != 0 && c <= TETRI_TB_END_OFFSET);

        /* Flush the run accumulated so far whenever a control byte or the
         * end of the string is reached -- a run never spans a style
         * change, matching how textbox_addtext() issued one
         * insert_with_tags() call per contiguous same-tag stretch. */
        if ((is_control || c == 0) && p > run_start)
            emit (&style, run_start, (size_t) (p - run_start), userdata);

        if (c == 0)
            break;

        if (!is_control) {
            if (p == run_start || (p > run_start && *(p - 1) != 0 && (unsigned char) *(p-1) <= TETRI_TB_END_OFFSET))
                run_start = p; /* start of a fresh run right after a control byte */
            continue;
        }

        /* control byte: apply it, then the run resumes at the next byte */
        run_start = p + 1;

        if (c == TETRI_TB_RESET) {
            style.color = gtet_colors[0];
            style.bold = style.italic = style.underline = 0;
            laststyle = style;
            last_color_code = 0;
            continue;
        }

        switch (c) {
        case TETRI_TB_BOLD:      style.bold = !style.bold; break;
        case TETRI_TB_ITALIC:    style.italic = !style.italic; break;
        case TETRI_TB_UNDERLINE: style.underline = !style.underline; break;
        default:
            if (c > TETRI_TB_C_END_OFFSET)
                break; /* not a recognized code -- ignore, matching the original */
            if (c == last_color_code) {
                /* toggle off: restore the color that was active before
                 * this color code was applied */
                style.color = laststyle.color;
                last_color_code = 0;
            } else {
                laststyle.color = style.color;
                last_color_code = c;
                style.color = gtet_colors[c - TETRI_TB_C_BEG_OFFSET];
            }
        }
    }
}

char *nocolor (char *str)
{
  static GString *ret = NULL;
  size_t len = strlen(str);
  signed char *scan, *p = NULL;

  if (!ret)
    ret = g_string_new("");

  g_string_assign(ret, str);

  p = scan = (signed char *)ret->str;
  while (*scan != 0)
  {
    if ((*scan > 0x1F) || (*scan < 0x0)) *p++ = *scan;
    scan++;
  }
  if (scan != p)
    g_string_truncate(ret, len - (scan - p));

  return ret->str;
}

/* Check if string is utf-8. If it isn't, convert from locale or iso8859-1. */
gchar* ensure_utf8(const char* str) {
    gchar* text;

    if(g_utf8_validate(str,-1,NULL)) {
        /* The string is valid utf-8, copy it. */
        text=g_strdup(str);
    } else {
        /* The string isn't valid utf-8, try locale. */
        text=g_locale_to_utf8(str,-1,NULL,NULL,NULL);
        if(!text) { /* The locale didn't work. Use ISO8859-1. */
            text=g_convert(str,-1,"UTF-8","ISO8859-1",NULL,NULL,NULL);
        }
    }
    /* Any random byte sequence is valid iso8859-1, so this won't happen.*/
    g_assert(text!=NULL && g_utf8_validate(text,-1,NULL));
    return text;
}

/* --- SDL_ttf-based rendering: replaces textbox_setup/textbox_addtext/
   adjust_bottom_text_view and the GtkTextTag machinery entirely. A
   single loaded font face is reused for every style combination via
   TTF_SetFontStyle (bold/italic/underline are all supported as
   synthesized style bits on one face -- no separate bold/italic font
   files needed). */

static TTF_Font *font = NULL;

/* --- rendered-text cache ---------------------------------------------
 * Almost every visible string (menu items, tab labels, field names,
 * Lines:/Level:, chat log history, ...) is re-rasterized via
 * TTF_RenderUTF8_Blended() from scratch on every single call to
 * misc_font_render() -- and every render_*() function in this port
 * calls it again every frame, even for text that hasn't changed since
 * the last one. FreeType rasterization is comparatively expensive, and
 * on hardware with no blit acceleration (real IRIX/O2) this was a
 * meaningful chunk of why the client felt slow, on top of the tile-blit
 * cost fields.c already addresses separately.
 *
 * A small fixed-size, round-robin cache keyed on (text, color, bold,
 * italic, underline) turns repeated calls for the same string+style
 * into a plain SDL_BlitSurface of an already-rendered surface. Not a
 * real LRU -- just "evict the oldest slot" -- which is enough given how
 * few *distinct* strings are ever on screen at once (tens, not
 * thousands); a cache miss just falls back to rendering fresh, same as
 * before this existed. */
#define TEXT_CACHE_SIZE 128
#define TEXT_CACHE_MAXLEN 255

typedef struct {
    char text[TEXT_CACHE_MAXLEN + 1];
    SDL_Color color;
    int bold, italic, underline;
    SDL_Surface *surface;
    int in_use;
} T_text_cache_entry;

static T_text_cache_entry text_cache[TEXT_CACHE_SIZE];
static int text_cache_next;

static void
text_cache_clear (void)
{
    int i;
    for (i = 0; i < TEXT_CACHE_SIZE; i++) {
        if (text_cache[i].surface)
            SDL_FreeSurface (text_cache[i].surface);
    }
    memset (text_cache, 0, sizeof (text_cache));
    text_cache_next = 0;
}

static int
text_cache_style_matches (const T_text_cache_entry *e, const T_textstyle *style)
{
    return e->color.r == style->color.r && e->color.g == style->color.g &&
           e->color.b == style->color.b &&
           e->bold == style->bold && e->italic == style->italic &&
           e->underline == style->underline;
    /* Deliberately not comparing color.a: most callers this port over
       leave it unset (TTF_RenderUTF8_Blended() never reads it in either
       SDL_ttf version -- glyph alpha comes from anti-aliasing, not the
       input color -- and real SDL 1.2's SDL_Color has no .a field at
       all), so it can hold indeterminate stack garbage that would
       otherwise cause spurious cache misses. */
}

static SDL_Surface *
text_cache_lookup (const char *text, const T_textstyle *style)
{
    int i;
    for (i = 0; i < TEXT_CACHE_SIZE; i++)
        if (text_cache[i].in_use &&
            text_cache_style_matches (&text_cache[i], style) &&
            strcmp (text_cache[i].text, text) == 0)
            return text_cache[i].surface;
    return NULL;
}

/* Always takes ownership of `surface` -- either stores it in the cache
   (freeing whatever it evicts) or, if `text` is too long to cache,
   frees `surface` itself. Callers never need to free it. */
static void
text_cache_insert (const char *text, const T_textstyle *style, SDL_Surface *surface)
{
    T_text_cache_entry *e;

    if (strlen (text) > TEXT_CACHE_MAXLEN) {
        SDL_FreeSurface (surface);
        return;
    }

    e = &text_cache[text_cache_next];
    text_cache_next = (text_cache_next + 1) % TEXT_CACHE_SIZE;

    if (e->surface)
        SDL_FreeSurface (e->surface);

    GTET_STRCPY (e->text, text, sizeof (e->text));
    e->color = style->color;
    e->bold = style->bold;
    e->italic = style->italic;
    e->underline = style->underline;
    e->surface = surface;
    e->in_use = 1;
}

int misc_font_init (const char *font_path, int size_px)
{
    if (TTF_Init () != 0)
        return -1;
    font = TTF_OpenFont (font_path, size_px);
    text_cache_clear ();
    return font ? 0 : -1;
}

void misc_font_cleanup (void)
{
    text_cache_clear ();
    if (font) {
        TTF_CloseFont (font);
        font = NULL;
    }
    TTF_Quit ();
}

int misc_font_line_height (void)
{
    return font ? TTF_FontLineSkip (font) : 0;
}

int misc_font_render (SDL_Surface *dst, int x, int y, const T_textstyle *style, const char *text, size_t len)
{
    char stackbuf[256];
    char *buf = stackbuf;
    SDL_Surface *rendered;
    SDL_Rect dstrect;
    int width;
    int stylebits;

    if (font == NULL || len == 0)
        return 0;

    /* T_formattedrun_fn hands us a non-nul-terminated substring -- copy
     * it out to a nul-terminated buffer for both the cache lookup key
     * and (on a miss) TTF_RenderUTF8_Blended, falling back to malloc for
     * the rare run longer than stackbuf. */
    if (len >= sizeof (stackbuf)) {
        buf = malloc (len + 1);
        if (!buf)
            return 0;
    }
    memcpy (buf, text, len);
    buf[len] = 0;

    rendered = text_cache_lookup (buf, style);
    if (rendered != NULL) {
        dstrect.x = x;
        dstrect.y = y;
        dstrect.w = rendered->w;
        dstrect.h = rendered->h;
        SDL_BlitSurface (rendered, NULL, dst, &dstrect);
        width = rendered->w;
        if (buf != stackbuf)
            free (buf);
        return width;
    }

    stylebits = TTF_STYLE_NORMAL;
    if (style->bold)      stylebits |= TTF_STYLE_BOLD;
    if (style->italic)    stylebits |= TTF_STYLE_ITALIC;
    if (style->underline) stylebits |= TTF_STYLE_UNDERLINE;
    TTF_SetFontStyle (font, stylebits);

    rendered = TTF_RenderUTF8_Blended (font, buf, style->color);
    if (rendered == NULL) {
        if (buf != stackbuf)
            free (buf);
        return 0;
    }

    dstrect.x = x;
    dstrect.y = y;
    dstrect.w = rendered->w;
    dstrect.h = rendered->h;
    SDL_BlitSurface (rendered, NULL, dst, &dstrect);
    width = rendered->w;

    /* text_cache_insert() takes ownership of `rendered` (or frees it
       itself if the string is too long to cache) -- don't also free it
       here. */
    text_cache_insert (buf, style, rendered);

    if (buf != stackbuf)
        free (buf);

    return width;
}

/* --- scrolling formatted-text log (see misc.h for why this is shared) --- */

struct textlog_append_ctx {
    T_logline *line;
};

static void
textlog_append_run_cb (const T_textstyle *style, const char *text, size_t len, void *userdata)
{
    struct textlog_append_ctx *ctx = userdata;
    T_logrun *run;

    if (ctx->line->runcount >= MISC_TEXTLOG_RUNS_PER_LINE)
        return;
    run = &ctx->line->runs[ctx->line->runcount++];
    run->style = *style;
    if (len >= sizeof (run->text))
        len = sizeof (run->text) - 1;
    memcpy (run->text, text, len);
    run->text[len] = 0;
}

void misc_textlog_append (T_textlog *log, const char *str)
{
    T_logline *line;
    struct textlog_append_ctx ctx;

    line = &log->lines[log->next];
    line->runcount = 0;
    ctx.line = line;
    misc_parse_formatted (str, textlog_append_run_cb, &ctx);

    log->next = (log->next + 1) % MISC_TEXTLOG_MAXLINES;
    if (log->count < MISC_TEXTLOG_MAXLINES)
        log->count++;
}

void misc_textlog_clear (T_textlog *log)
{
    log->count = 0;
    log->next = 0;
}

void misc_textlog_render (SDL_Surface *dst, const SDL_Rect *rect, const T_textlog *log)
{
    int line_h = misc_font_line_height ();
    int visible = rect->h / (line_h ? line_h : 1);
    int i, y;
    int first; /* ring-buffer index of the first line to draw */

    if (visible > log->count)
        visible = log->count;
    if (visible <= 0)
        return;

    /* Most recent line is at (log->next - 1); walk back `visible` lines
     * from there to find where to start, then draw forward top-to-bottom
     * so the newest line ends up at the bottom -- a chat window, not a
     * top-down log. */
    first = (log->next - visible + MISC_TEXTLOG_MAXLINES) % MISC_TEXTLOG_MAXLINES;

    y = rect->y + rect->h - visible * line_h;
    for (i = 0; i < visible; i++) {
        const T_logline *line = &log->lines[(first + i) % MISC_TEXTLOG_MAXLINES];
        int x = rect->x;
        int r;
        for (r = 0; r < line->runcount; r++)
            x += misc_font_render (dst, x, y, &line->runs[r].style,
                                    line->runs[r].text, strlen (line->runs[r].text));
        y += line_h;
    }
}
