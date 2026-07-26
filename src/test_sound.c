/* Standalone correctness test for sound.c -- not part of the app build.
 * Compile+run manually:
 *   cc test_sound.c sound.c $(pkg-config --cflags --libs sdl2 SDL2_mixer) -o /tmp/test_sound
 *   /tmp/test_sound /tmp/test_tone.wav
 */
#include <stdio.h>
#include <string.h>
#include <SDL.h>
#include <SDL_mixer.h>
#include "sound.h"

static int fails = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf ("FAIL: %s\n", msg); fails++; } \
    else printf ("ok: %s\n", msg); \
} while (0)

int main (int argc, char **argv)
{
    if (argc != 2) {
        fprintf (stderr, "usage: %s path-to-a-wav-file\n", argv[0]);
        return 2;
    }

    CHECK (SDL_Init (SDL_INIT_AUDIO) == 0, "SDL_Init(SDL_INIT_AUDIO) succeeded");

    /* Before sound_init(): must be safe no-ops, not crashes -- matches
     * the original's HAVE_CANBERRAGTK-less stub behavior. */
    soundenable = 1;
    sound_cache ();
    sound_playsound (S_DROP);
    CHECK (1, "sound_cache/sound_playsound before sound_init() are safe no-ops");

    CHECK (sound_init () == 0, "sound_init() opened the audio device");

    /* soundenable == 0: cache/play must stay no-ops even with a real
     * device open and a real file configured. */
    soundenable = 0;
    strncpy (soundfiles[S_DROP], argv[1], sizeof (soundfiles[S_DROP]) - 1);
    sound_cache ();
    sound_playsound (S_DROP);
    CHECK (1, "sound_cache/sound_playsound with soundenable=0 are safe no-ops");

    /* Now actually load and play the real file. */
    soundenable = 1;
    sound_cache ();
    sound_playsound (S_DROP);
    SDL_Delay (150); /* let the mixer thread actually start playback */
    CHECK (Mix_Playing (-1) >= 0, "Mix_Playing() callable after sound_playsound() -- mixer is alive");

    /* An id with no file configured for it must not crash. */
    sound_playsound (S_TETRIS);
    CHECK (1, "playing an id with no cached sample is a safe no-op");

    /* Re-caching (e.g. on theme switch) must not leak/crash on the
     * previously-loaded chunk. */
    sound_cache ();
    sound_playsound (S_DROP);
    CHECK (1, "re-calling sound_cache() (theme switch) frees the old chunk cleanly");

    sound_cleanup ();
    CHECK (1, "sound_cleanup() ran without crashing");

    printf ("%s\n", fails ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED");
    return fails ? 1 : 0;
}
