#include <SDL.h>

/* SDL port: every GTK dialog (connect, connecting-progress, team,
 * preferences, about, plus the nested "press a key" prompt inside
 * preferences) becomes state rendered/hit-tested by the main loop,
 * matching fields.c/partyline.c's "store state, redraw every frame"
 * pattern rather than separate GtkWindows. Only one dialog is ever
 * meaningfully interactive at a time -- same as the original, where
 * opening a second copy of an already-open dialog just re-presents the
 * existing window -- except the connect dialog and its connecting-
 * progress overlay, which (as in the original) can be open together:
 * the progress overlay sits on top of the connect dialog while a
 * connection attempt is in flight. */

extern void teamdialog_new (void);
extern void connectdialog_new (void);
extern void connectingdialog_destroy (void);
extern void connectingdialog_new (void);
extern void connectdialog_connected (void);
extern void prefdialog_new (void);
extern void aboutdialog_new (void);

/* True if any dialog is currently open; the main loop uses this to
   decide whether to render/route input to the dialog layer instead of
   (or on top of) the current game page. */
int dialog_is_open (void);

/* Draws whichever dialog(s) are currently open into rect, topmost last. */
void dialog_render (SDL_Surface *dst, const SDL_Rect *rect);

/* Input routing while a dialog is open, mirroring fields.c's gmsg
   input / partyline.c's entry-box input split of responsibility. */
void dialog_textinput (const char *text);
void dialog_backspace (void);
void dialog_keydown (int keycode);
void dialog_click (int x, int y);

/* Drives the connecting-progress pulse; a no-op when nothing needs it.
   Call once per frame regardless of dialog state. */
void dialog_tick (void);
