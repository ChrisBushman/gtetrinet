/* Standalone correctness test for sched.c -- not part of the app build.
 * Compile+run manually:
 *   cc test_sched.c sched.c $(pkg-config --cflags --libs sdl2) -o /tmp/test_sched && /tmp/test_sched
 */
#include <stdio.h>
#include <SDL.h>
#include "sched.h"

static int repeat_fire_count = 0;
static int oneshot_fired = 0;
static int removed_should_never_fire = 0;
static unsigned int reschedule_from_callback_id = 0;
static int reschedule_from_callback_fired = 0;

static int
repeat_cb (void *data)
{
    (void) data;
    repeat_fire_count++;
    return repeat_fire_count < 3; /* fire 3 times total, then stop */
}

static int
oneshot_cb (void *data)
{
    (void) data;
    oneshot_fired = 1;
    return 0;
}

static int
never_fire_cb (void *data)
{
    (void) data;
    removed_should_never_fire = 1;
    return 0;
}

static int
reschedule_target_cb (void *data)
{
    (void) data;
    reschedule_from_callback_fired = 1;
    return 0;
}

/* Mirrors tetrinet_nextblock()'s real pattern: a callback that, on firing,
   registers a *new* timer (this is what can trigger sched.c's internal
   realloc mid-tick and is the scenario the use-after-free fix guards
   against). */
static int
reschedule_from_callback_cb (void *data)
{
    (void) data;
    reschedule_from_callback_id = sched_timeout_add (5, reschedule_target_cb, NULL);
    return 0;
}

static int
fails = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf ("FAIL: %s\n", msg); fails++; } \
    else printf ("ok: %s\n", msg); \
} while (0)

static void
pump_until (Uint32 ms)
{
    Uint32 start = SDL_GetTicks ();
    while (SDL_GetTicks () - start < ms) {
        sched_tick ();
        SDL_Delay (1);
    }
    sched_tick ();
}

int
main (void)
{
    unsigned int id_never, id_repeat, id_oneshot;

    SDL_Init (0);

    /* Repeat semantics: return nonzero -> reschedule; verify it fires
       the expected number of times then stops on its own. */
    id_repeat = sched_timeout_add (10, repeat_cb, NULL);
    CHECK (id_repeat != 0, "repeat timer got a nonzero id");

    /* One-shot semantics: return 0 -> removed after first fire. */
    id_oneshot = sched_timeout_add (10, oneshot_cb, NULL);

    /* Removed before it ever gets a chance to fire. */
    id_never = sched_timeout_add (10, never_fire_cb, NULL);
    sched_timer_remove (id_never);

    /* A callback that registers a brand new timer from inside sched_tick
       -- exercises the realloc/use-after-free fix. */
    sched_timeout_add (10, reschedule_from_callback_cb, NULL);

    pump_until (200);

    CHECK (repeat_fire_count == 3, "repeat timer fired exactly 3 times then stopped");
    CHECK (oneshot_fired == 1, "one-shot timer fired exactly once");
    CHECK (removed_should_never_fire == 0, "removed-before-firing timer never fired");
    CHECK (reschedule_from_callback_id != 0, "callback-registered timer got a valid id");
    CHECK (reschedule_from_callback_fired == 1, "timer registered from within another callback fired correctly");

    /* sched_timer_remove with id 0 or an already-fired id: must not crash. */
    sched_timer_remove (0);
    sched_timer_remove (id_oneshot);
    sched_tick ();
    CHECK (1, "removing id 0 and an already-fired id did not crash");

    sched_remove_all ();
    sched_tick ();
    CHECK (1, "sched_remove_all + tick on an empty scheduler did not crash");

    printf ("\n%s\n", fails ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED");
    return fails ? 1 : 0;
}
