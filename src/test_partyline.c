/* Standalone correctness test for partyline.c -- not part of the app
 * build. Compile+run manually:
 *   cc test_partyline.c partyline.c misc.c -I. \
 *     $(pkg-config --cflags --libs glib-2.0 sdl2 SDL2_ttf) \
 *     -o /tmp/test_partyline && /tmp/test_partyline
 */
#include <stdio.h>
#include <string.h>
#include <SDL.h>
#include "client.h"
#include "tetrinet.h"
#include "partyline.h"
#include "misc.h"

/* --- stubs for externs partyline.c needs but doesn't own --- */
int connected;
char server[128];
int playernum;
int list_issued;
char sent_lines[16][300];
int sent_line_count;

void tetrinet_playerline (const char *text)
{
    if (sent_line_count < 16) {
        int idx = sent_line_count++;
        GTET_STRCPY (sent_lines[idx], text, sizeof (sent_lines[0]));
    }
}

static int fails = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf ("FAIL: %s\n", msg); fails++; } \
    else printf ("ok: %s\n", msg); \
} while (0)

int main (void)
{
    char *names[3] = {"Alice", "Bob", "Carolyn"};
    char *teams[3] = {"Red", "", "Red"};
    int numbers[3] = {0, 1, 2};
    char *specs[2] = {"Spectator1", "Zed"};

    SDL_Init (0);
    misc_font_init ("../data/fonts/DejaVuSans.ttf", 14);
    CHECK (partyline_page_new () == 0, "partyline_page_new succeeded");

    /* --- channel list parsing (tetrinet-server/jetrix bracket style) --- */
    connected = 1;

    partyline_add_channel ("(1)lobby[3/10]{IDLE} the main lobby");
    partyline_add_channel ("(2)proteam[FULL]{INGAME} pros only");
    stop_list ();

    /* Verify via partyline_channel_activate(), the only externally
       observable read of display_channels[]: activating index 0 must
       send "/join #<name-of-first-parsed-channel>". */
    sent_line_count = 0;
    partyline_channel_activate (0);
    CHECK (sent_line_count == 1, "activating channel 0 sent exactly one line");
    CHECK (sent_line_count == 1 && strcmp (sent_lines[0], "/join #lobby") == 0,
           "channel 0 parsed name is 'lobby' (from the first /list line)");

    sent_line_count = 0;
    partyline_channel_activate (1);
    CHECK (sent_line_count == 1 && strcmp (sent_lines[0], "/join #proteam") == 0,
           "channel 1 parsed name is 'proteam' (second /list line, FULL variant)");

    /* Out-of-range index must be a safe no-op, not a crash/garbage join. */
    sent_line_count = 0;
    partyline_channel_activate (99);
    CHECK (sent_line_count == 0, "activating an out-of-range channel index is a safe no-op");

    /* --- player list + nick completion --- */
    partyline_playerlist (numbers, names, teams, 3, specs, 2);

    partyline_connectstatus (1);
    partyline_textinput ("al");
    partyline_keydown (SDLK_TAB);
    CHECK (strcmp (partyline_entrytext (), "Alice: ") == 0,
           "TAB-completing 'al' against the player list resolves to 'Alice: '");

    partyline_entryfocus (); /* clears the entry for the next test */
    partyline_textinput ("ze");
    partyline_keydown (SDLK_TAB);
    CHECK (strcmp (partyline_entrytext (), "Zed: ") == 0,
           "TAB-completing 'ze' falls through to the spectator list and resolves to 'Zed: '");

    partyline_entryfocus ();
    partyline_textinput ("nobody-matches-this");
    partyline_keydown (SDLK_TAB);
    CHECK (strcmp (partyline_entrytext (), "nobody-matches-this") == 0,
           "TAB with no matching nick leaves the entry text unchanged");

    /* --- history recall --- */
    partyline_entryfocus ();
    partyline_textinput ("first message");
    partyline_keydown (SDLK_RETURN);
    partyline_entryfocus ();
    partyline_textinput ("second message");
    partyline_keydown (SDLK_RETURN);
    partyline_entryfocus ();
    partyline_keydown (SDLK_UP);
    CHECK (strcmp (partyline_entrytext (), "second message") == 0,
           "Up-arrow recalls the most recent submitted history entry");
    partyline_keydown (SDLK_UP);
    CHECK (strcmp (partyline_entrytext (), "first message") == 0,
           "a second Up-arrow recalls the entry before that");
    partyline_keydown (SDLK_DOWN);
    CHECK (strcmp (partyline_entrytext (), "second message") == 0,
           "Down-arrow after two Ups moves forward one entry again");

    /* --- disabled entry box must reject input --- */
    partyline_connectstatus (0);
    partyline_textinput ("should be ignored");
    CHECK (strcmp (partyline_entrytext (), "second message") == 0,
           "text input while the entry box is disabled (disconnected) is ignored");

    printf ("%s\n", fails ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED");
    return fails ? 1 : 0;
}
