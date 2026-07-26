/* Standalone visual test for fields.c -- not part of the app build.
 * Stubs the tetrinet.c/config.c globals fields.c reads, feeds it fake
 * game state, renders one frame, and saves it as a PNG for inspection.
 *
 * Compile+run manually from src/:
 *   cc test_fields_visual.c fields.c misc.c -I. \
 *     $(pkg-config --cflags --libs glib-2.0 sdl2 SDL2_image SDL2_ttf) \
 *     -DGTETRINET_DATA='"'"$(pwd)/.."'"' \
 *     -o /tmp/test_fields_visual && /tmp/test_fields_visual
 */
#include <string.h>
#include <glib.h>
#include <SDL.h>
#include <SDL_image.h>
#include "gtet_config.h"
#include "tetrinet.h"
#include "tetris.h"
#include "fields.h"
#include "misc.h"

/* --- stubs for globals fields.c reads but doesn't own --- */
char blocksfile[1024];
int bsize;
GString *currenttheme;
char specialblocks[256];
int specialblocknum;
int ingame, playing, paused;

void config_loadtheme (const gchar *themedir)
{
    (void) themedir;
    fprintf (stderr, "test stub: config_loadtheme should not be called (default theme must load)\n");
}

int main (void)
{
    SDL_Surface *screen;
    FIELD ownfield;
    FIELD oppfield;
    TETRISBLOCK block;
    int x, y;

    SDL_Init (SDL_INIT_VIDEO);

    if (misc_font_init ("../data/fonts/DejaVuSans.ttf", 14) != 0) {
        printf ("FAIL: could not load font\n");
        return 1;
    }

    currenttheme = g_string_new ("");
    strcpy (blocksfile, "../themes/default/blocks.png");
    bsize = 20;

    if (fields_init () != 0) {
        printf ("FAIL: fields_init failed to load tileset\n");
        return 1;
    }

    fields_page_new ();
    printf ("layout: %dx%d\n", fields_screen_width (), fields_screen_height ());

    screen = SDL_CreateRGBSurface (0, fields_screen_width (), fields_screen_height (), 32, 0,0,0,0);
    SDL_FillRect (screen, NULL, SDL_MapRGB (screen->format, 40, 40, 40));

    /* Fake own field: a partially filled bottom, matching how a real
       mid-game board looks (some solid rows, some gaps). */
    memset (ownfield, 0, sizeof (ownfield));
    for (y = FIELDHEIGHT - 4; y < FIELDHEIGHT; y++)
        for (x = 0; x < FIELDWIDTH; x++)
            if (!(y == FIELDHEIGHT - 1 && x == 5)) /* leave one gap */
                ownfield[y][x] = ((x + y) % 7) + 1;
    fields_drawfield (0, ownfield);

    /* Fake opponent fields: lighter fill so we can see 5 distinct boards. */
    memset (oppfield, 0, sizeof (oppfield));
    for (y = FIELDHEIGHT - 2; y < FIELDHEIGHT; y++)
        for (x = 0; x < FIELDWIDTH; x++)
            oppfield[y][x] = ((x) % 7) + 1;
    fields_drawfield (1, oppfield);
    fields_drawfield (2, oppfield);
    fields_drawfield (3, oppfield);
    fields_drawfield (4, oppfield);
    fields_drawfield (5, oppfield);

    fields_setlabel (0, "Alice", "Red Team", 1);
    fields_setlabel (1, "Bob", NULL, 2);
    fields_setlabel (2, "Carol", "Blue Team", 3);
    fields_setlabel (3, NULL, NULL, 0); /* not playing */
    fields_setlabel (4, "Eve", NULL, 5);
    fields_setlabel (5, "Frank", "Red Team", 6);

    memset (block, 0, sizeof (block));
    block[0][1] = block[1][1] = block[1][0] = block[1][2] = 3; /* T piece, color 3 */
    fields_drawnextblock (block);

    specialblocknum = 4;
    specialblocks[0] = 1;
    specialblocks[1] = 3;
    specialblocks[2] = 5;
    specialblocks[3] = 2;
    fields_drawspecials ();

    fields_setlines (42);
    fields_setlevel (7);
    fields_setactivelevel (7);

    /* Exercise the formatting protocol through the real public API. */
    {
        char buf[256];
        snprintf (buf, sizeof (buf), "Alice %cattacks%c Bob with %cAdd Line%c!",
                  TETRI_TB_BOLD, TETRI_TB_BOLD, TETRI_TB_C_BRIGHT_RED, TETRI_TB_C_BRIGHT_RED);
        fields_attdefmsg (buf);
        fields_attdeffmt ("%s used a Clear Line special", "Carol");
    }
    fields_gmsgadd ("<Alice> good luck everyone");
    fields_gmsgadd ("<Bob> you too!");
    fields_gmsginput (1);
    fields_gmsg_textinput ("typing a message...");

    fields_render (screen);

    SDL_SaveBMP (screen, "/tmp/fields_test.bmp");
    printf ("wrote /tmp/fields_test.bmp\n");

    fields_cleanup ();
    misc_font_cleanup ();
    return 0;
}
