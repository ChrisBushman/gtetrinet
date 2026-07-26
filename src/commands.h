/* SDL port: GtkActionGroup/GtkUIManager's declarative action+menu/toolbar
 * XML (make_menus()) is replaced by a plain array the (not yet written)
 * SDL main loop can render and hit-test each frame, matching
 * fields.c/winlist.c/partyline.c's "store state, redraw every frame"
 * pattern. The original's "DetachPage" entry (detach_command(),
 * move_current_page_to_window()) is dropped: it depended on
 * GtkNotebook's OS-level tab-detach-into-its-own-window mechanism, which
 * has no SDL equivalent, and upstream's own comment already flagged it
 * as "not ready". handle_links() (GtkAboutDialog link-click callback) is
 * dropped for the same reason the About dialog itself moves to
 * dialogs.c: no GtkAboutDialog to attach it to. */
typedef enum {
    CMD_CONNECT,
    CMD_DISCONNECT,
    CMD_CHANGE_TEAM,
    CMD_START_GAME,
    CMD_PAUSE_GAME,
    CMD_END_GAME,
    CMD_PREFERENCES,
    CMD_ABOUT,
    CMD_EXIT,
    CMD_COUNT
} T_command_id;

typedef struct {
    T_command_id id;
    const char *label;
    int visible;
    int enabled;
} T_menuitem;

void commands_init (void);
int commands_menu_count (void);
const T_menuitem *commands_menu_item (int index);

/* Called by the main loop once it has hit-tested a click against the
 * rendered menu; a no-op if the item is currently hidden/disabled. */
void commands_activate (T_command_id id);

void commands_checkstate (void);

void show_start_button (void);
void show_stop_button (void);
void show_connect_button (void);
void show_disconnect_button (void);
