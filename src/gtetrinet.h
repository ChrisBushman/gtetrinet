/* SDL port: this header used to declare GtkWidget- and GSettings-typed
 * globals and GDK-event-typed signal handlers (app, settings*, keypress/
 * keyrelease, get_current_notebook_page, move_current_page_to_window).
 * All of that is gone along with GTK itself -- move_current_page_to_
 * window()/get_current_notebook_page() had no callers left once
 * commands.c's detach_command() was dropped (see commands.h), and
 * keypress/keyrelease are replaced by gtetrinet.c's own internal SDL
 * event-loop routing (no longer anything other files need to call). */

#define APPID PACKAGE
#define APPNAME "GTetrinet"
#define APPVERSION VERSION

#define ORIGINAL 0
#define TETRIFAST 1

extern int gamemode;

extern void destroymain (void);
extern void show_fields_page (void);
extern void show_partyline_page (void);
extern void unblock_keyboard_signal (void);
