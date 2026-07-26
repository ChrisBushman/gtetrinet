#include <SDL.h>

extern int timestampsenable;
extern int list_enabled;

/* SDL port: same "store state, redraw every frame" pattern as
 * fields.c/winlist.c. partyline_page_new()'s GtkWidget* return (no
 * widget tree) becomes int (0 on success). partyline_switch_entryfocus()
 * from the original header is dropped: it had zero implementation and
 * zero callers anywhere in the codebase (confirmed by grep) -- genuinely
 * dead API surface, not something to carry forward. */
int partyline_page_new (void);
void partyline_page_cleanup (void);
void partyline_connectstatus (int status);
void partyline_namelabel (char *nick, char *team);
void partyline_status (char *status);
void partyline_text (const char *text);
void partyline_fmt (const char *text, ...);
void partyline_playerlist (int *numbers, char **names, char **teams, int n, char **specs, int sn);
void partyline_entryfocus (void);
void partyline_add_channel (char *line);

/* int (*)(void), matching sched_timer_func's cast-from-zero-arg idiom
 * already used throughout tetrinet.c for every other g_timeout_add-
 * turned-sched_timeout_add callback (see sched.h). */
int partyline_update_channel_list (void);

void partyline_more_channel_lines (void);
void partyline_clear_list_channel (void);
void partyline_joining_channel (const char *channel);
void stop_list (void);
void partyline_show_channel_list (int show);

/* New: replaces channel_activated(), which fired on a GtkTreeView row
 * double-click. Call with the clicked row's index into the currently
 * displayed channel list once the (not yet written) main loop has
 * mouse-hit-testing against partyline_render()'s layout. */
void partyline_channel_activate (int index);

/* New: the main loop calls this once per frame to draw the chat log,
 * entry box, player list, and (if enabled) channel list. */
void partyline_render (SDL_Surface *dst, const SDL_Rect *rect);

/* New: input routing, same division of responsibility as fields.c's
 * gmsg input -- the main loop's SDL_TEXTINPUT/SDL_KEYDOWN handling
 * calls these while the partyline entry box has focus; partyline.c owns
 * the actual text buffer, history, and completion state. */
void partyline_textinput (const char *text);
void partyline_backspace (void);
void partyline_keydown (int keycode);

/* New: read-only access to the current entry-box text, matching
 * fields.h's fields_gmsginputtext() accessor for the same reason (the
 * main loop needs to display it, and it's the natural way to make the
 * buffer's contents testable). */
const char *partyline_entrytext (void);
