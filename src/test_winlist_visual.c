/* Standalone visual test for winlist.c -- not part of the app build.
 * Compile+run manually from src/:
 *   cc test_winlist_visual.c winlist.c misc.c -I. \
 *     $(pkg-config --cflags --libs glib-2.0 sdl2 SDL2_image SDL2_ttf) \
 *     -DGTETPIXMAPSDIR='"'"$(pwd)/../icons"'"' \
 *     -o /tmp/test_winlist_visual && /tmp/test_winlist_visual
 */
#include <string.h>
#include <SDL.h>
#include "winlist.h"
#include "misc.h"

int main (void)
{
    SDL_Surface *screen;
    SDL_Rect rect;

    SDL_Init (SDL_INIT_VIDEO);

    if (misc_font_init ("../data/fonts/DejaVuSans.ttf", 16) != 0) {
        printf ("FAIL: could not load font\n");
        return 1;
    }
    if (winlist_page_new () != 0) {
        printf ("FAIL: winlist_page_new could not load icons\n");
        return 1;
    }

    winlist_additem (1, "Alice (Red Team)", 12500);
    winlist_additem (1, "Frank (Red Team)", 9800);
    winlist_additem (0, "Bob", 6200);
    winlist_additem (0, "Carol", 3100);

    screen = SDL_CreateRGBSurface (0, 400, 300, 32, 0,0,0,0);
    SDL_FillRect (screen, NULL, SDL_MapRGB (screen->format, 20, 20, 20));

    rect.x = 10; rect.y = 10; rect.w = 380; rect.h = 280;
    winlist_render (screen, &rect);

    SDL_SaveBMP (screen, "/tmp/winlist_test.bmp");
    printf ("wrote /tmp/winlist_test.bmp\n");

    winlist_page_cleanup ();
    misc_font_cleanup ();
    return 0;
}
