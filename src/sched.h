/*
 *  GTetrinet (SDL port)
 *
 *  A small, portable timer scheduler standing in for GLib's main-loop
 *  timeout mechanism (g_timeout_add/g_source_remove), which the GTK build
 *  relied on to drive gameplay pacing (fall speed, next-block delay,
 *  line-clear flash, moderator/partyline polling). There is no GLib main
 *  loop in the SDL port -- sched_tick() is called once per frame from the
 *  app's own event loop instead, and fires any timer whose deadline has
 *  passed.
 *
 *  The callback contract intentionally matches GLib's GSourceFunc exactly
 *  (nonzero return = "reschedule again after the same interval", zero
 *  return = "remove this timer"), so every existing g_timeout_add/
 *  g_source_remove call site in tetrinet.c becomes a 1:1 mechanical swap:
 *
 *      movedowntimeout = g_timeout_add (duration, (GSourceFunc)tetrinet_timeout, NULL);
 *   -> movedowntimeout = sched_timeout_add (duration, (sched_timer_func)tetrinet_timeout, NULL);
 *
 *      g_source_remove (movedowntimeout);
 *   -> sched_timer_remove (movedowntimeout);
 *
 *  This file has no GTK/GLib dependency. It uses SDL_GetTicks() as its
 *  time base, matching the AmuletsArmor SDL port's own timing approach
 *  (proven working on Windows/macOS/OS X Tiger PPC/IRIX 6.5 this session).
 */
#ifndef SCHED_H
#define SCHED_H

typedef int (*sched_timer_func) (void *data);

/* Register a timer to fire interval_ms milliseconds from now. Returns an
   id usable with sched_timer_remove(), or 0 on failure (0 is never a
   valid id, matching GLib's own guarantee about source ids). */
unsigned int sched_timeout_add (unsigned int interval_ms, sched_timer_func func, void *data);

/* Cancel a pending timer. Safe to call with id 0, or an id that has
   already fired and self-removed (both are silent no-ops). */
void sched_timer_remove (unsigned int id);

/* Call once per frame from the main loop, after SDL_Init(). Fires any
   timer whose deadline has passed. If a timer's callback returns nonzero,
   it is rescheduled for interval_ms after its *previous* deadline (not
   after "now"), so a busy frame doesn't drift the cadence -- matching
   GLib's own catch-up behavior for GSourceFunc timeouts. Returns nonzero
   if at least one timer fired this call -- the main loop uses this to
   decide whether anything timer-driven (fall step, line-clear, partyline
   refresh) could have changed what's on screen, without re-rendering on
   every single frame regardless of whether anything actually happened. */
int sched_tick (void);

/* Remove every pending timer. Call at disconnect/game-end so stale
   timers (e.g. a fall-speed timer from a game that just ended) can't
   fire into a state that's no longer valid -- mirrors the effect of the
   scattered g_source_remove cleanup calls already at each such site,
   without having to track every id by hand. */
void sched_remove_all (void);

#endif
