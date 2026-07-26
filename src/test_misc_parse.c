/* Standalone correctness test for misc_parse_formatted() -- not part of
 * the app build. Compile+run manually:
 *   cc test_misc_parse.c misc.c $(pkg-config --cflags --libs glib-2.0 sdl2 SDL2_ttf) -o /tmp/test_misc_parse && /tmp/test_misc_parse
 */
#include <stdio.h>
#include <string.h>
#include <SDL.h>
#include "misc.h"

static int fails = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf ("FAIL: %s\n", msg); fails++; } \
    else printf ("ok: %s\n", msg); \
} while (0)

struct run_record {
    char text[128];
    T_textstyle style;
};

static struct run_record runs[32];
static int run_count;

static void
collect_run (const T_textstyle *style, const char *text, size_t len, void *userdata)
{
    (void) userdata;
    if (run_count >= 32) return;
    if (len >= sizeof (runs[0].text)) len = sizeof (runs[0].text) - 1;
    memcpy (runs[run_count].text, text, len);
    runs[run_count].text[len] = 0;
    runs[run_count].style = *style;
    run_count++;
}

static void
reset (void)
{
    run_count = 0;
    memset (runs, 0, sizeof (runs));
}

int
main (void)
{
    char buf[128];

    /* Plain text, no formatting at all -- should be a single run. */
    reset ();
    misc_parse_formatted ("hello world", collect_run, NULL);
    CHECK (run_count == 1, "plain text produces exactly one run");
    CHECK (strcmp (runs[0].text, "hello world") == 0, "plain text run content matches input");
    CHECK (runs[0].style.bold == 0 && runs[0].style.italic == 0 && runs[0].style.underline == 0,
           "plain text run has no styling");

    /* Bold toggle: \x02bold\x02 normal */
    reset ();
    snprintf (buf, sizeof (buf), "%cbold%c normal", TETRI_TB_BOLD, TETRI_TB_BOLD);
    misc_parse_formatted (buf, collect_run, NULL);
    CHECK (run_count == 2, "bold-toggle-on/off produces two runs");
    if (run_count == 2) {
        CHECK (strcmp (runs[0].text, "bold") == 0, "first run is the bolded text");
        CHECK (runs[0].style.bold == 1, "first run is actually marked bold");
        CHECK (strcmp (runs[1].text, " normal") == 0, "second run is the unbolded remainder");
        CHECK (runs[1].style.bold == 0, "second run is not bold");
    }

    /* Color toggle-on then toggle-off with the SAME code restores the
       prior color (this is the "if (tmp == last) restore" branch in the
       original textbox_addtext, exercised here via misc_parse_formatted). */
    reset ();
    snprintf (buf, sizeof (buf), "before%cred%cafter",
              TETRI_TB_C_BRIGHT_RED, TETRI_TB_C_BRIGHT_RED);
    misc_parse_formatted (buf, collect_run, NULL);
    CHECK (run_count == 3, "color-on/off produces three runs");
    if (run_count == 3) {
        CHECK (runs[0].style.color.r == runs[2].style.color.r &&
               runs[0].style.color.g == runs[2].style.color.g &&
               runs[0].style.color.b == runs[2].style.color.b,
               "color restored after toggling the same color code off matches the original color");
        CHECK (runs[1].style.color.r == 255 && runs[1].style.color.g == 0 && runs[1].style.color.b == 0,
               "toggled-on color run is bright red (0xFF0000)");
    }

    /* Reset code (0xFF) clears all active styling. */
    reset ();
    snprintf (buf, sizeof (buf), "%cbold%c%cplain", TETRI_TB_BOLD, TETRI_TB_RESET, ' ');
    /* the %c ' ' above is a no-op placeholder; real string: BOLD "bold" RESET "plain" */
    snprintf (buf, sizeof (buf), "%cbold%cplain", TETRI_TB_BOLD, TETRI_TB_RESET);
    misc_parse_formatted (buf, collect_run, NULL);
    CHECK (run_count == 2, "reset code produces two runs");
    if (run_count == 2) {
        CHECK (runs[0].style.bold == 1, "run before reset is bold");
        CHECK (runs[1].style.bold == 0, "run after reset is not bold");
        CHECK (strcmp (runs[1].text, "plain") == 0, "text after reset is correct");
    }

    /* Multiple simultaneous styles combine on one run. */
    reset ();
    snprintf (buf, sizeof (buf), "%c%cboth", TETRI_TB_BOLD, TETRI_TB_ITALIC);
    misc_parse_formatted (buf, collect_run, NULL);
    CHECK (run_count == 1, "combined bold+italic on same text is one run");
    if (run_count == 1)
        CHECK (runs[0].style.bold == 1 && runs[0].style.italic == 1, "run has both bold and italic set");

    /* Empty string: no runs at all, must not crash. */
    reset ();
    misc_parse_formatted ("", collect_run, NULL);
    CHECK (run_count == 0, "empty string produces zero runs");

    /* Consecutive control bytes with no text between them produce no
       empty runs. */
    reset ();
    snprintf (buf, sizeof (buf), "%c%c%ctext", TETRI_TB_BOLD, TETRI_TB_ITALIC, TETRI_TB_UNDERLINE);
    misc_parse_formatted (buf, collect_run, NULL);
    CHECK (run_count == 1, "back-to-back control codes with no text between emit no empty runs");

    printf ("\n%s\n", fails ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED");
    return fails ? 1 : 0;
}
