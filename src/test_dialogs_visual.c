/* Standalone visual test for dialogs.c -- not part of the app build.
 * Compile+run manually from src/ (see test_dialogs.c's header comment
 * for the sandboxed theme-data directory this also relies on):
 *   cc test_dialogs_visual.c dialogs.c commands.c config.c fields.c \
 *     misc.c sound.c winlist.c partyline.c -I. \
 *     -DVERSION='"0.11.9"' -DPACKAGE='"gtetrinet"' \
 *     -DGTETRINET_DATA='"/tmp/gtet_test_data"' -DGTETPIXMAPSDIR='"/tmp/icons"' \
 *     $(pkg-config --cflags --libs glib-2.0 sdl2 SDL2_image SDL2_ttf) -lSDL2_mixer \
 *     -o /tmp/test_dialogs_visual && /tmp/test_dialogs_visual
 */
#include <stdio.h>
#include <string.h>
#include <glib.h>
#include <SDL.h>
#include "client.h"
#include "tetrinet.h"
#include "gtet_config.h"
#include "misc.h"
#include "sound.h"
#include "partyline.h"
#include "dialogs.h"

int connected;
int ingame, playing, paused;
int moderator, spectating;
int playernum;
char team[128], nick[128], specpassword[128];
char server[128];
int gamemode;
char specialblocks[256];
int specialblocknum;
int list_issued;

void client_init (const char *s G_GNUC_UNUSED, const char *n G_GNUC_UNUSED) {}
void client_disconnect (void) {}
void client_outmessage (enum outmsg_type msgtype G_GNUC_UNUSED, char *str G_GNUC_UNUSED) {}
void tetrinet_changeteam (const char *newteam G_GNUC_UNUSED) {}
void tetrinet_redrawfields (void) {}
void fieldslabelupdate (void) {}
void tetrinet_playerline (const char *text G_GNUC_UNUSED) {}
void destroymain (void) {}

static void
snap (SDL_Surface *screen, const char *name)
{
    char path[256];
    g_snprintf (path, sizeof (path), "/tmp/%s.bmp", name);
    SDL_SaveBMP (screen, path);
    printf ("wrote %s\n", path);
}

int main (void)
{
    SDL_Surface *screen;
    SDL_Rect rect = {0, 0, 640, 480};

    SDL_Init (SDL_INIT_VIDEO);
    misc_font_init ("../data/fonts/DejaVuSans.ttf", 14);
    screen = SDL_CreateRGBSurface (0, 640, 480, 32, 0, 0, 0, 0);
    currenttheme = g_string_new (GTETRINET_DATA "/themes/default/");

    GTET_STRCPY (server, "tetrinet.example.com", sizeof (server));
    GTET_STRCPY (nick, "Alice", sizeof (nick));
    GTET_STRCPY (team, "Red", sizeof (team));
    memcpy (keys, defaultkeys, K_NUM * sizeof (int));

    connectdialog_new ();
    SDL_FillRect (screen, NULL, SDL_MapRGB (screen->format, 20, 20, 20));
    dialog_render (screen, &rect);
    snap (screen, "dialog_connect");
    dialog_keydown (SDLK_ESCAPE);

    connectdialog_new ();
    connectingdialog_new ();
    SDL_FillRect (screen, NULL, SDL_MapRGB (screen->format, 20, 20, 20));
    dialog_render (screen, &rect);
    snap (screen, "dialog_connecting");
    connectingdialog_destroy ();
    dialog_keydown (SDLK_ESCAPE);

    teamdialog_new ();
    SDL_FillRect (screen, NULL, SDL_MapRGB (screen->format, 20, 20, 20));
    dialog_render (screen, &rect);
    snap (screen, "dialog_team");
    dialog_keydown (SDLK_ESCAPE);

    aboutdialog_new ();
    SDL_FillRect (screen, NULL, SDL_MapRGB (screen->format, 20, 20, 20));
    dialog_render (screen, &rect);
    snap (screen, "dialog_about");
    dialog_keydown (SDLK_ESCAPE);

    prefdialog_new ();
    SDL_FillRect (screen, NULL, SDL_MapRGB (screen->format, 20, 20, 20));
    dialog_render (screen, &rect);
    snap (screen, "dialog_prefs_themes");

    {
        int line_h = misc_font_line_height ();
        int box_x = rect.x + (rect.w - 480) / 2;
        int tabs_y = rect.y + (rect.h - 380) / 2 + 8 + line_h + 8;

        dialog_click (box_x + 8 + 104 + 10, tabs_y + 3); /* Partyline */
        SDL_FillRect (screen, NULL, SDL_MapRGB (screen->format, 20, 20, 20));
        dialog_render (screen, &rect);
        snap (screen, "dialog_prefs_partyline");

        dialog_click (box_x + 8 + 2 * 104 + 10, tabs_y + 3); /* Keyboard */
        SDL_FillRect (screen, NULL, SDL_MapRGB (screen->format, 20, 20, 20));
        dialog_render (screen, &rect);
        snap (screen, "dialog_prefs_keyboard");

        dialog_click (box_x + 8 + 3 * 104 + 10, tabs_y + 3); /* Sound */
        SDL_FillRect (screen, NULL, SDL_MapRGB (screen->format, 20, 20, 20));
        dialog_render (screen, &rect);
        snap (screen, "dialog_prefs_sound");
    }
    dialog_keydown (SDLK_ESCAPE);

    printf ("done\n");
    return 0;
}
