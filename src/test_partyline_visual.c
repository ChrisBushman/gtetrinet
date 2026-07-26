/* Standalone visual test for partyline.c -- not part of the app build.
 * Compile+run manually from src/:
 *   cc test_partyline_visual.c partyline.c misc.c -I. \
 *     $(pkg-config --cflags --libs glib-2.0 sdl2 SDL2_image SDL2_ttf) \
 *     -o /tmp/test_partyline_visual && /tmp/test_partyline_visual
 */
#include <stdio.h>
#include <SDL.h>
#include "client.h"
#include "tetrinet.h"
#include "partyline.h"
#include "misc.h"

int connected;
char server[128];
int playernum;
int list_issued;

void tetrinet_playerline (const char *text)
{
    (void) text;
}

int main (void)
{
    SDL_Surface *screen;
    SDL_Rect rect;
    char *names[3] = {"Alice", "Bob", "Carolyn"};
    char *teams[3] = {"Red", "", "Red"};
    int numbers[3] = {0, 1, 2};
    char *specs[1] = {"Zed"};

    SDL_Init (SDL_INIT_VIDEO);

    if (misc_font_init ("../data/fonts/DejaVuSans.ttf", 14) != 0) {
        printf ("FAIL: could not load font\n");
        return 1;
    }
    if (partyline_page_new () != 0) {
        printf ("FAIL: partyline_page_new failed\n");
        return 1;
    }

    connected = 1;
    partyline_connectstatus (1);
    partyline_namelabel ("Alice", "Red Team");
    partyline_status ("Game in progress");
    partyline_joining_channel ("lobby");

    partyline_text ("* Alice has joined the channel");
    partyline_fmt ("<%s> hello there!", "Bob");
    partyline_text ("* Carolyn has left the channel");

    partyline_playerlist (numbers, names, teams, 3, specs, 1);

    partyline_add_channel ("(1)lobby[3/10]{IDLE} the main lobby");
    partyline_add_channel ("(2)proteam[FULL]{INGAME} pros only");
    stop_list ();
    partyline_show_channel_list (1);

    partyline_textinput ("hi everyone");

    screen = SDL_CreateRGBSurface (0, 640, 400, 32, 0,0,0,0);
    SDL_FillRect (screen, NULL, SDL_MapRGB (screen->format, 20, 20, 20));

    rect.x = 10; rect.y = 10; rect.w = 620; rect.h = 380;
    partyline_render (screen, &rect);

    SDL_SaveBMP (screen, "/tmp/partyline_test.bmp");
    printf ("wrote /tmp/partyline_test.bmp\n");

    partyline_page_cleanup ();
    misc_font_cleanup ();
    return 0;
}
