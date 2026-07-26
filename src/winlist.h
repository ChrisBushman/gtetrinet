#include <SDL.h>

/* SDL port: same "store state, fields_render()-style redraw every
 * frame" pattern as fields.c/partyline.c-to-come, not GTK's retained
 * GtkListStore/GtkTreeView. winlist_page_new()'s GtkWidget* return
 * (there is no widget tree) is replaced with void -- it just loads the
 * team/alone icons once. */
int winlist_page_new (void);
void winlist_page_cleanup (void);
void winlist_clear (void);
void winlist_additem (int team, char *name, int score);
void winlist_focus (void);

/* New: called once per frame by whichever screen shows the win list. */
void winlist_render (SDL_Surface *dst, const SDL_Rect *rect);
