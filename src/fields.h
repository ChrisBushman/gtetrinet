#include <SDL.h>

/* SDL port: fields.c owns all the board/UI *state* (field contents,
 * labels, chat/battle-log scrollback, next-block preview, specials
 * bar), same as the GTK build did, but no longer draws anything the
 * moment state changes -- GTK's per-widget expose/draw events don't
 * exist here. Instead every function below just updates stored state,
 * and fields_render() (called once per frame by the app's main loop)
 * redraws the whole scene from that state -- the standard SDL/game
 * pattern of "redraw everything every frame" rather than GTK's
 * damage-tracked partial redraw. This keeps every other file's calls
 * into fields_* (tetrinet.c, commands.c, partyline.c) working exactly
 * as before; only the *implementation* changed. */

extern int fields_init (void);
extern void fields_cleanup (void);

/* Replaces fields_page_new()'s GtkWidget* return (there is no widget
 * tree) -- lays out where each of the 6 fields/next-block/specials bar/
 * text areas sit on screen, in a fixed window of FIELDS_SCREEN_W x
 * FIELDS_SCREEN_H pixels for the *current* theme's block size. Call
 * again if the theme (and so the block size) changes. */
extern void fields_page_new (void);
extern void fields_page_destroy_contents (void);

extern void fields_drawfield (int field, FIELD newfield);
extern void fields_setlabel (int field, char *name, char *team, int num);
extern void fields_setspeciallabel (char *label);
extern void fields_drawspecials (void);
extern void fields_drawnextblock (TETRISBLOCK block);
extern void fields_attdefmsg (char *text);
extern void fields_attdeffmt (const char *fmt, ...);
extern void fields_attdefclear (void);
extern void fields_setlines (int l);
extern void fields_setlevel (int l);
extern void fields_setactivelevel (int l);
extern void fields_gmsgadd (const char *str);
extern void fields_gmsgclear (void);
extern void fields_gmsginput (int i);
extern void fields_gmsginputclear (void);
extern void fields_gmsginputactivate (int i);
extern const char *fields_gmsginputtext (void);

/* New: the main loop calls this once per frame to actually draw
 * everything (fields, next-block, specials, labels, attdef/gmsg logs)
 * onto dst. Replaces fields_expose_event/fields_nextpiece_expose/
 * fields_specials_expose, which GTK invoked individually per widget on
 * damage; here it's one pass over all of it. */
extern void fields_render (SDL_Surface *dst);

/* New: routes typed text and backspace into the game-message input box
 * while it's visible (fields_gmsginput(1) was called and
 * fields_gmsginputactivate(1)/gmsgstate hasn't cleared it yet) --
 * SDL_TEXTINPUT/backspace-KEYDOWN handling belongs in the app's input
 * loop, not here, but the input *buffer* is owned by fields.c, same as
 * fields_gmsginputtext() already implied in the GTK build (there it was
 * GtkEntry's own buffer; here it's explicit). */
extern void fields_gmsg_textinput (const char *text);
extern void fields_gmsg_backspace (void);

/* Total pixel size of the window fields_render() expects to draw into,
 * for BLOCKSIZE = bsize (see gtet_config.h) -- valid only after
 * fields_page_new(). */
extern int fields_screen_width (void);
extern int fields_screen_height (void);
