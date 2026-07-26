/*
 *  GTetrinet (SDL port) -- see sched.h for the design rationale.
 */
#include <stdlib.h>
#include <string.h>
#include <SDL.h>

#include "sched.h"

struct timer {
    unsigned int id;
    unsigned int interval_ms;
    Uint32 deadline;
    sched_timer_func func;
    void *data;
    int active; /* 0 once fired-and-removed or explicitly cancelled */
};

static struct timer *timers = NULL;
static int timers_count = 0;
static int timers_capacity = 0;
static unsigned int next_id = 1;

unsigned int
sched_timeout_add (unsigned int interval_ms, sched_timer_func func, void *data)
{
    struct timer *t;

    if (timers_count == timers_capacity) {
        int new_capacity = timers_capacity ? timers_capacity * 2 : 16;
        struct timer *grown = realloc (timers, (size_t) new_capacity * sizeof (struct timer));
        if (grown == NULL)
            return 0;
        timers = grown;
        timers_capacity = new_capacity;
    }

    t = &timers[timers_count++];
    t->id = next_id++;
    t->interval_ms = interval_ms;
    t->deadline = SDL_GetTicks () + interval_ms;
    t->func = func;
    t->data = data;
    t->active = 1;

    return t->id;
}

void
sched_timer_remove (unsigned int id)
{
    int i;

    if (id == 0)
        return;

    for (i = 0; i < timers_count; i++) {
        if (timers[i].id == id) {
            timers[i].active = 0;
            return;
        }
    }
}

void
sched_remove_all (void)
{
    timers_count = 0;
}

void
sched_tick (void)
{
    Uint32 now;
    int i;

    /* Iterate by index, not a cached pointer/count: a callback may itself
       call sched_timeout_add() (tetrinet_nextblocktimeout does exactly
       this), which can realloc() the array out from under a stale
       pointer and grow timers_count mid-loop. Re-reading timers_count
       each iteration picks up newly-added timers too, matching GLib's
       own main loop (a timeout added from within a callback is eligible
       to fire in a later iteration once its deadline arrives). */
    now = SDL_GetTicks ();
    for (i = 0; i < timers_count; i++) {
        sched_timer_func func;
        void *data;

        if (!timers[i].active)
            continue;
        /* now - deadline rather than now >= deadline: correct even
           across Uint32 wraparound (~49 days of uptime), same trick
           SDL_TICKS_PASSED uses. */
        if ((Sint32) (now - timers[i].deadline) < 0)
            continue;

        /* Copy out everything the callback's return handling needs
           before calling it: func() may itself call sched_timeout_add(),
           which can realloc() the array and move it, making a cached
           "struct timer *" into this array a dangling pointer the
           instant the callback returns. Re-indexing timers[i] afterward
           (not dereferencing a pre-call pointer) is what makes this
           safe -- realloc() preserves element order/contents, it just
           may move the base address, so the index itself stays valid. */
        func = timers[i].func;
        data = timers[i].data;

        if (func (data)) {
            /* Reschedule from the previous deadline, not "now", so a
               delayed frame doesn't push the cadence later and later --
               matches GLib's GSourceFunc catch-up behavior. */
            timers[i].deadline += timers[i].interval_ms;
        } else {
            timers[i].active = 0;
        }
    }

    /* Compact out inactive entries only now, after the loop above has
       finished entirely -- doing it mid-loop would shift indices out
       from under sched_timer_remove() calls a callback makes against
       *other*, not-yet-processed timers (tetrinet.c does this: e.g.
       tetrinet_nextblock() calls tetrinet_removetimeout() to cancel
       movedowntimeout before arming nextblocktimeout). The timer count
       here is always tiny (a handful at once), so doing this every tick
       rather than batching it costs nothing measurable. */
    if (timers_count > 0) {
        int write = 0;
        for (i = 0; i < timers_count; i++) {
            if (timers[i].active)
                timers[write++] = timers[i];
        }
        timers_count = write;
    }
}
