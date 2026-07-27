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
#include <string.h>
#include <time.h>
#if defined(__sgi)
#include <unistd.h>
#endif
#if defined(__APPLE__)
#include <unistd.h>
#include <mach-o/dyld.h>
#endif
#include <glib.h>
#include <SDL.h>
#include <SDL_image.h>

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
#include "sound.h"
#include "dialogs.h"
#include "sched.h"

int gamemode = ORIGINAL;

typedef enum {
    PAGE_FIELDS,
    PAGE_PARTYLINE,
    PAGE_WINLIST,
    PAGE_COUNT
} T_page;

static const char *page_names[PAGE_COUNT] = { "Playing Fields", "Partyline", "Winlist" };

static int running = 1;
static T_page current_page = PAGE_FIELDS;

#if SDL_MAJOR_VERSION >= 2
static SDL_Window *window;
#endif
static SDL_Surface *screen;
static SDL_Surface *fields_canvas;

static int win_w, win_h;
static SDL_Rect menubar_rect, tabbar_rect, content_rect;

/* 30fps rather than 60: halves the per-frame render workload (~1500+
 * tile blits plus every text label get redrawn from scratch each
 * frame -- see fields_render()) for no real playability cost in a
 * tetris-style game, and matters on hardware with no blit acceleration
 * (real IRIX/O2). */
#define FRAME_INTERVAL_MS 33

#define MAX_MENU_RECTS 16
static SDL_Rect menu_rects[MAX_MENU_RECTS];
static T_command_id menu_rect_ids[MAX_MENU_RECTS];
static int menu_rect_count;

static SDL_Rect tab_rects[PAGE_COUNT];

/* --- video (SDL 1.2 / SDL2 compatibility) ---------------------------
 * Every other file in this port draws onto a plain SDL_Surface (never
 * a Renderer/Texture) specifically so this is the only place that
 * needs to know which SDL major version it's built against -- SDL2's
 * SDL_GetWindowSurface() gives back the same kind of legacy software
 * surface SDL 1.2's SDL_SetVideoMode() always returned. */

static int
video_init (int w, int h)
{
#if SDL_MAJOR_VERSION >= 2
    window = SDL_CreateWindow (APPNAME, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                w, h, 0);
    if (!window)
        return -1;
    screen = SDL_GetWindowSurface (window);
    return screen ? 0 : -1;
#else
    screen = SDL_SetVideoMode (w, h, 32, SDL_SWSURFACE);
    if (!screen)
        return -1;
    SDL_WM_SetCaption (APPNAME, APPNAME);
    return 0;
#endif
}

static void
video_flip (void)
{
#if SDL_MAJOR_VERSION >= 2
    SDL_UpdateWindowSurface (window);
#else
    SDL_Flip (screen);
#endif
}

static void
set_window_icon (void)
{
    /* gtetrinet.png is the repo's top-level app icon/about-logo asset,
       not one of the per-widget pixmaps under icons/ (team.png,
       alone.png) that GTETPIXMAPSDIR points at -- it lives directly
       under GTETRINET_DATA instead. */
    SDL_Surface *icon = IMG_Load (GTETRINET_DATA "/gtetrinet.png");
    if (!icon)
        return;
#if SDL_MAJOR_VERSION >= 2
    SDL_SetWindowIcon (window, icon);
#else
    SDL_WM_SetIcon (icon, NULL);
#endif
    SDL_FreeSurface (icon);
}

/* --- game-message input submit --------------------------------------
 * Replaces gmsginput_activate(), previously wired to GtkEntry's
 * "activate" signal (Enter). fields.c owns the input buffer itself
 * (fields_gmsginputtext()/fields_gmsginputclear()); the *decision* of
 * what to do with a submitted line was always app-level logic, not
 * something that belonged inside fields.c. */

static void
gmsg_submit (void)
{
    char buf[512];
    const char *s = fields_gmsginputtext ();

    if (strlen (s) > 0) {
        if (strncmp ("/me ", s, 4) == 0)
            g_snprintf (buf, sizeof (buf), "* %s %s", nick, s + 4);
        else
            g_snprintf (buf, sizeof (buf), "<%s> %s", nick, s);
        client_outmessage (OUT_GMSG, buf);
    }
    fields_gmsginputclear ();
    fields_gmsginput (0);
    gmsgstate = 0;
}

/* --- page switching ---------------------------------------------------
 * switch_page(PAGE_FIELDS) refocusing the *partyline* entry when not in
 * gmsg mode looks odd, but it faithfully matches the original
 * switch_focus()'s case 0 -- not a typo introduced by this port. */

static void
switch_page (T_page page)
{
    current_page = page;
    if (!connected)
        return;
    switch (page) {
    case PAGE_FIELDS:
        if (gmsgstate) fields_gmsginputactivate (1);
        else partyline_entryfocus ();
        break;
    case PAGE_PARTYLINE:
        partyline_entryfocus ();
        break;
    case PAGE_WINLIST:
        winlist_focus ();
        break;
    default:
        break;
    }
}

/* called when the main window is destroyed */
void
destroymain (void)
{
    running = 0;
}

void
show_fields_page (void)
{
    switch_page (PAGE_FIELDS);
}

void
show_partyline_page (void)
{
    switch_page (PAGE_PARTYLINE);
}

/* SDL has no GTK-style signal-blocking to undo; gmsgstate itself is
   already what routes keys away from the game while typing a message,
   so there's nothing left for this to do. Kept only because tetrinet.c
   still calls it (see gtet_config.h-era forward-declare comments
   elsewhere in the port for why call sites weren't all touched). */
void
unblock_keyboard_signal (void)
{
}

/* --- layout: menu bar / tab bar / content area ---------------------- */

static void
layout_chrome (void)
{
    int line_h = misc_font_line_height ();

    menubar_rect.x = 0;
    menubar_rect.y = 0;
    menubar_rect.w = win_w;
    menubar_rect.h = line_h + 14;

    tabbar_rect.x = 0;
    tabbar_rect.y = menubar_rect.h;
    tabbar_rect.w = win_w;
    tabbar_rect.h = line_h + 10;

    content_rect.x = 0;
    content_rect.y = menubar_rect.h + tabbar_rect.h;
    content_rect.w = win_w;
    content_rect.h = win_h - content_rect.y;
}

static void
render_menubar (SDL_Surface *dst)
{
    T_textstyle style;
    int line_h = misc_font_line_height ();
    int x = 4, y = 4;
    int i;

    style.color.r = style.color.g = style.color.b = 0xFF;
   
    style.bold = style.italic = style.underline = 0;

    SDL_FillRect (dst, &menubar_rect, SDL_MapRGB (dst->format, 24, 24, 24));

    menu_rect_count = 0;
    for (i = 0; i < commands_menu_count () && menu_rect_count < MAX_MENU_RECTS; i++) {
        const T_menuitem *item = commands_menu_item (i);
        SDL_Rect r;
        int tw;

        if (!item->visible)
            continue;

        /* No text-width-measurement API exists yet (see misc.h) -- this
           is a rough monospace-ish estimate, a known simplification
           like the ones already documented in fields.c/partyline.c. */
        tw = (int) strlen (item->label) * 8 + 16;
        r.x = x; r.y = y; r.w = tw; r.h = line_h + 6;

        SDL_FillRect (dst, &r, SDL_MapRGB (dst->format, item->enabled ? 55 : 35,
                                           item->enabled ? 55 : 35, item->enabled ? 55 : 35));
        misc_font_render (dst, r.x + 6, r.y + 3, &style, item->label, strlen (item->label));

        menu_rects[menu_rect_count] = r;
        menu_rect_ids[menu_rect_count] = item->id;
        menu_rect_count++;

        x += tw + 4;
    }
}

static void
render_tabbar (SDL_Surface *dst)
{
    T_textstyle style;
    int tab_w = win_w / PAGE_COUNT;
    int i;

    style.color.r = style.color.g = style.color.b = 0xFF;
   
    style.bold = style.italic = style.underline = 0;

    for (i = 0; i < PAGE_COUNT; i++) {
        SDL_Rect r;
        int active = ((int) current_page == i);

        r.x = i * tab_w;
        r.y = tabbar_rect.y;
        r.w = (i == PAGE_COUNT - 1) ? (win_w - r.x) : tab_w;
        r.h = tabbar_rect.h;

        SDL_FillRect (dst, &r, SDL_MapRGB (dst->format, active ? 45 : 28, active ? 45 : 28, active ? 45 : 28));
        style.bold = active;
        misc_font_render (dst, r.x + 6, r.y + 3, &style, page_names[i], strlen (page_names[i]));

        tab_rects[i] = r;
    }
}

static void
ensure_fields_canvas (void)
{
    int w = fields_screen_width ();
    int h = fields_screen_height ();

    if (fields_canvas)
        SDL_FreeSurface (fields_canvas);
    fields_canvas = SDL_CreateRGBSurface (0, w, h, 32, 0, 0, 0, 0);
}

static void
render_frame (void)
{
    SDL_FillRect (screen, NULL, SDL_MapRGB (screen->format, 10, 10, 10));

    render_menubar (screen);
    render_tabbar (screen);

    switch (current_page) {
    case PAGE_FIELDS:
        if (fields_canvas) {
            SDL_Rect dst_pos = { content_rect.x, content_rect.y, 0, 0 };
            SDL_FillRect (fields_canvas, NULL, SDL_MapRGB (fields_canvas->format, 10, 10, 10));
            fields_render (fields_canvas);
            SDL_BlitSurface (fields_canvas, NULL, screen, &dst_pos);
        }
        break;
    case PAGE_PARTYLINE:
        partyline_render (screen, &content_rect);
        break;
    case PAGE_WINLIST:
        winlist_render (screen, &content_rect);
        break;
    default:
        break;
    }

    if (dialog_is_open ()) {
        SDL_Rect full = { 0, 0, win_w, win_h };
        dialog_render (screen, &full);
    }

    video_flip ();
}

/* --- input routing ---------------------------------------------------
 * Priority, topmost first: an open dialog always wins; otherwise Alt+
 * 1/2/3 page-switch (matching the original's GDK_MOD1_MASK check in
 * gtetrinet_key()); otherwise whichever page is active. Backspace is
 * routed to each module's dedicated backspace entry point rather than
 * through its keydown handler, matching the convention already
 * established by fields.c/partyline.c/dialogs.c's own headers. */

static void
route_click (int x, int y)
{
    int i;

    if (dialog_is_open ()) {
        dialog_click (x, y);
        return;
    }

    for (i = 0; i < menu_rect_count; i++)
        if (x >= menu_rects[i].x && x < menu_rects[i].x + menu_rects[i].w &&
            y >= menu_rects[i].y && y < menu_rects[i].y + menu_rects[i].h) {
            commands_activate (menu_rect_ids[i]);
            return;
        }

    for (i = 0; i < PAGE_COUNT; i++)
        if (x >= tab_rects[i].x && x < tab_rects[i].x + tab_rects[i].w &&
            y >= tab_rects[i].y && y < tab_rects[i].y + tab_rects[i].h) {
            switch_page ((T_page) i);
            return;
        }

    /* Channel-list row clicks aren't hit-tested (partyline_render()
       doesn't expose per-row rects) -- joining a channel by typing
       "/join #name" in the partyline entry always works, matching the
       original's own "parsing can't be perfect, so make sure they can
       do it by hand" fallback. */
}

static void
route_keydown (int keycode)
{
    if (dialog_is_open ()) {
        if (keycode == SDLK_BACKSPACE)
            dialog_backspace ();
        else
            dialog_keydown (keycode);
        return;
    }

    if (SDL_GetModState () & KMOD_ALT) {
        if (keycode == SDLK_1) { switch_page (PAGE_FIELDS); return; }
        if (keycode == SDLK_2) { switch_page (PAGE_PARTYLINE); return; }
        if (keycode == SDLK_3) { switch_page (PAGE_WINLIST); return; }
    }

    if (current_page == PAGE_FIELDS) {
        if (gmsgstate) {
            if (keycode == SDLK_RETURN || keycode == SDLK_KP_ENTER)
                gmsg_submit ();
            else if (keycode == SDLK_BACKSPACE)
                fields_gmsg_backspace ();
            return;
        }
        if (ingame && keycode == keys[K_GAMEMSG]) {
            gmsgstate = 1;
            fields_gmsginputactivate (1);
            fields_gmsginput (1);
            return;
        }
        tetrinet_key (keycode);
    }
    else if (current_page == PAGE_PARTYLINE) {
        if (keycode == SDLK_BACKSPACE)
            partyline_backspace ();
        else
            partyline_keydown (keycode);
    }
}

static void
route_keyup (int keycode)
{
    if (dialog_is_open ())
        return;
    if (current_page == PAGE_FIELDS && !gmsgstate)
        tetrinet_upkey (keycode);
}

static void
route_textinput (const char *text)
{
    if (dialog_is_open ()) {
        dialog_textinput (text);
        return;
    }
    if (current_page == PAGE_FIELDS && gmsgstate)
        fields_gmsg_textinput (text);
    else if (current_page == PAGE_PARTYLINE)
        partyline_textinput (text);
}

/* Whether this frame is worth actually rendering and pushing to the
 * display. video_flip() serializes the whole window through the plain
 * X11 protocol every time it's called (confirmed: this SDL build has
 * no MIT-SHM support at all), which dominates real per-frame cost on
 * hardware with no blit acceleration (real IRIX/O2) far more than the
 * rendering work itself -- so the win is in not calling it at all on a
 * frame where nothing could plausibly have changed, not in rendering
 * faster.
 *
 * This used to also treat "we're in a state where anything could be
 * changing on its own" (connected, ingame, or typing a game message) as
 * an unconditional redraw -- deliberately coarse, since auditing every
 * individual state mutator across fields.c/partyline.c/dialogs.c/
 * winlist.c/commands.c/config.c to know exactly when something visible
 * changed looked like dozens of call sites and an easy one to miss.
 * But that meant every single frame rendered (up to 30fps) for the
 * entire time a player was connected, which is most of a session --
 * the flag barely gated anything in the case that actually matters.
 *
 * tick_fired and socket_activity close that gap without the broad
 * audit: every timer-driven visual change in this app (fall step,
 * line-clear, partyline refresh -- see sched.c callers) goes through
 * sched_timeout_add(), so sched_tick()'s return value already says
 * "something timer-driven might have changed"; every server-driven
 * change (opponent moves, chat, stats) arrives through
 * client_poll_socket()'s single dispatch path. Between those two and
 * had_event (local input), the only remaining unconditional case is
 * dialog_is_open() -- the connecting-dialog's progress pulse animates
 * on a wall-clock timer of its own (see connectingdialog_tick()), not
 * through sched.c, so it still needs an unconditional redraw while any
 * dialog is up. */
static int
should_render (int had_event, int tick_fired, int socket_activity)
{
    if (had_event || tick_fired || socket_activity)
        return 1;
    if (dialog_is_open ())
        return 1;
    return 0;
}

#if defined(__APPLE__)
/* A double-clicked (or `open`-launched) .app bundle's working directory
 * is whatever Finder/launchd happened to set it to -- unlike a plain
 * command-line binary, there's no guarantee it's anywhere near this
 * executable, or even the same every time. GTETRINET_DATA/GTETPIXMAPSDIR
 * are "." for a bundle build (GTET_RELATIVE_DATA_DIR in CMakeLists.txt),
 * so make "." mean something real: if this executable's own path looks
 * like it's inside a bundle (.../Contents/MacOS/gtetrinet), chdir into
 * .../Contents/Resources before anything (theme/font/icon loading)
 * tries to open a path relative to cwd. Silently does nothing for the
 * plain dev binary, which was never built with GTET_RELATIVE_DATA_DIR
 * on and isn't inside a bundle to begin with. */
static void
gtet_chdir_to_bundle_resources (void)
{
    char exe_path[4096];
    uint32_t size = sizeof (exe_path);
    char *macos_pos;
    char resources_path[4096];

    if (_NSGetExecutablePath (exe_path, &size) != 0)
        return;

    macos_pos = strstr (exe_path, "/Contents/MacOS/");
    if (!macos_pos)
        return;

    *macos_pos = 0;
    g_snprintf (resources_path, sizeof (resources_path), "%s/Contents/Resources", exe_path);
    chdir (resources_path);
}
#endif

int
main (int argc, char *argv[])
{
#if defined(__APPLE__)
    gtet_chdir_to_bundle_resources ();
#endif

    char *option_connect = NULL, *option_nick = NULL, *option_team = NULL, *option_pass = NULL;
    int option_spec = 0;
    int i;
    Uint32 last_tick;

    for (i = 1; i < argc; i++) {
        if ((strcmp (argv[i], "-c") == 0 || strcmp (argv[i], "--connect") == 0) && i + 1 < argc)
            option_connect = argv[++i];
        else if ((strcmp (argv[i], "-n") == 0 || strcmp (argv[i], "--nickname") == 0) && i + 1 < argc)
            option_nick = argv[++i];
        else if ((strcmp (argv[i], "-t") == 0 || strcmp (argv[i], "--team") == 0) && i + 1 < argc)
            option_team = argv[++i];
        else if (strcmp (argv[i], "-s") == 0 || strcmp (argv[i], "--spectate") == 0)
            option_spec = 1;
        else if ((strcmp (argv[i], "-p") == 0 || strcmp (argv[i], "--password") == 0) && i + 1 < argc)
            option_pass = argv[++i];
    }

    srand ((unsigned int) time (NULL));

    if (SDL_Init (SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        fprintf (stderr, "SDL_Init failed: %s\n", SDL_GetError ());
        return 1;
    }
#ifdef IMG_INIT_PNG
    /* IMG_Init()/IMG_INIT_PNG were added in SDL_image 1.2.11 (2012);
       Nekoware's IRIX package predates that -- IMG_Load() there (and on
       every other SDL_image version) auto-detects format and works
       fine without ever calling IMG_Init() explicitly, so this is a
       pure optimization, safe to skip entirely when unavailable. */
    IMG_Init (IMG_INIT_PNG);
#endif
#if SDL_MAJOR_VERSION >= 2
    SDL_StartTextInput ();
#else
    SDL_EnableUNICODE (1);
    SDL_EnableKeyRepeat (SDL_DEFAULT_REPEAT_DELAY, SDL_DEFAULT_REPEAT_INTERVAL);
#endif

    /* Bundled font lives at data/fonts/, not GTETRINET_DATA/fonts/
       directly -- "data" here is this repo's own top-level directory
       name, unrelated to the GTETRINET_DATA build constant. */
    if (misc_font_init (GTETRINET_DATA "/data/fonts/DejaVuSans.ttf", 14) != 0)
        fprintf (stderr, "Warning: could not load bundled font, text will not render.\n");

    if (sound_init () != 0)
        soundenable = 0;

    config_loadconfig ();
    config_loadconfig_keys ();
    config_loadconfig_themes ();

    if (fields_init () != 0) {
        fprintf (stderr, "fields_init failed\n");
        return 1;
    }
    fields_page_new ();
    ensure_fields_canvas ();

    partyline_page_new ();
    if (winlist_page_new () != 0)
        fprintf (stderr, "Warning: winlist icons failed to load.\n");

    win_w = fields_screen_width ();
    if (win_w < 560) win_w = 560;
    win_h = misc_font_line_height () * 2 + 24 + fields_screen_height ();
    if (win_h < 500) win_h = 500;

    if (video_init (win_w, win_h) != 0) {
        fprintf (stderr, "video_init failed: %s\n", SDL_GetError ());
        return 1;
    }
    set_window_icon ();
    layout_chrome ();

    commands_init ();
    partyline_show_channel_list (list_enabled);
    commands_checkstate ();

    if (option_nick) GTET_O_STRCPY (nick, option_nick);
    if (option_team) GTET_O_STRCPY (team, option_team);
    if (option_pass) GTET_O_STRCPY (specpassword, option_pass);
    if (option_spec) spectating = 1;
    if (option_connect)
        client_init (option_connect, nick);

    app_mainloop_running = 1;
    last_tick = SDL_GetTicks ();

    /* should_render()'s first call could plausibly see no event yet
       (nothing queued the instant the loop starts) and no connected/
       ingame/dialog state either -- force the initial frame explicitly
       rather than relying on the window's own expose event arriving
       before the first SDL_Delay. */
    render_frame ();

    while (running) {
        SDL_Event ev;
        Uint32 now;
        int had_event = 0;

        while (SDL_PollEvent (&ev)) {
            had_event = 1;
            switch (ev.type) {
            case SDL_QUIT:
                running = 0;
                break;
            case SDL_KEYDOWN:
                route_keydown (ev.key.keysym.sym);
#if SDL_MAJOR_VERSION < 2
                if (ev.key.keysym.unicode >= 32 && ev.key.keysym.unicode < 127) {
                    char buf[2];
                    buf[0] = (char) ev.key.keysym.unicode;
                    buf[1] = 0;
                    route_textinput (buf);
                }
#endif
                break;
            case SDL_KEYUP:
                route_keyup (ev.key.keysym.sym);
                break;
#if SDL_MAJOR_VERSION >= 2
            case SDL_TEXTINPUT:
                route_textinput (ev.text.text);
                break;
            case SDL_WINDOWEVENT:
                /* SDL2's equivalent of SDL 1.2's SDL_VIDEOEXPOSE below --
                   already counted as an "event happened" via had_event,
                   nothing more to do here than let that fall through. */
                break;
#else
            case SDL_VIDEOEXPOSE:
                /* No window-manager backing store on plain X11 without a
                   compositor (the normal case on real IRIX) -- a window
                   that was covered and is now exposed needs an explicit
                   redraw, or it can show stale/garbage pixels. Already
                   counted via had_event; this case only exists so it
                   doesn't fall into "unhandled", which is harmless
                   either way but documents that it's intentional. */
                break;
#endif
            case SDL_MOUSEBUTTONDOWN:
                if (ev.button.button == SDL_BUTTON_LEFT)
                    route_click (ev.button.x, ev.button.y);
                break;
            default:
                break;
            }
        }

        {
            int tick_fired = sched_tick ();
            int socket_activity;

            client_poll_connect ();
            socket_activity = client_poll_socket ();
            dialog_tick ();

            if (should_render (had_event, tick_fired, socket_activity))
                render_frame ();
        }

        now = SDL_GetTicks ();
        if (now - last_tick < FRAME_INTERVAL_MS) {
#if defined(__sgi)
            /* This SDL 1.2 build's SDL_Delay() busy-waits on real IRIX
               instead of yielding the CPU (confirmed on real O2 hardware)
               -- every main-loop iteration was burning a full CPU-time
               slice for the whole FRAME_INTERVAL_MS regardless of
               should_render()'s decision, since the "delay" never
               actually blocked. usleep() is a real libc/kernel sleep on
               IRIX and isn't affected by this SDL bug, so use it
               directly here instead of going through SDL_Delay(). */
            usleep ((useconds_t) (FRAME_INTERVAL_MS - (now - last_tick)) * 1000);
#else
            SDL_Delay (FRAME_INTERVAL_MS - (now - last_tick));
#endif
        }
        last_tick = SDL_GetTicks ();
    }

    client_disconnect ();
    fields_cleanup ();
    winlist_page_cleanup ();
    sound_cleanup ();
    misc_font_cleanup ();
    if (fields_canvas)
        SDL_FreeSurface (fields_canvas);

    SDL_Quit ();

    return 0;
}
