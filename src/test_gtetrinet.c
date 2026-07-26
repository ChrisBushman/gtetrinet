/* Standalone correctness test for gtetrinet.c's input-routing/dispatch
 * logic -- not part of the app build. Rather than linking the whole
 * app, this #includes gtetrinet.c directly (renaming its main() out of
 * the way) so the test can call its static route_ / switch_page /
 * gmsg_submit functions in isolation, with every other module stubbed.
 * Compile+run manually:
 *   cc test_gtetrinet.c -I. \
 *     -DVERSION='"0.11.9"' -DPACKAGE='"gtetrinet"' \
 *     -DGTETRINET_DATA='"/tmp/gtet_test_data"' -DGTETPIXMAPSDIR='"/tmp/icons"' \
 *     $(pkg-config --cflags --libs glib-2.0 sdl2 SDL2_image SDL2_ttf) \
 *     -o /tmp/test_gtetrinet && /tmp/test_gtetrinet
 */
#include <stdio.h>
#include <string.h>

#define main gtetrinet_main_unused
#include "gtetrinet.c"
#undef main

/* --- stubs for every module gtetrinet.c calls into --- */

int connected;
int ingame, playing, paused;
int moderator, spectating;
int playernum;
char team[128], nick[128], specpassword[128];
char server[128];
char specialblocks[256];
int specialblocknum;
int list_issued;
int gmsgstate;
int list_enabled;
int keys[K_NUM];
int defaultkeys[K_NUM];
int app_mainloop_running;

int tetrinet_key_calls, tetrinet_key_arg;
int tetrinet_key (int keyval) { tetrinet_key_calls++; tetrinet_key_arg = keyval; return 1; }
int tetrinet_upkey_calls, tetrinet_upkey_arg;
void tetrinet_upkey (int keyval) { tetrinet_upkey_calls++; tetrinet_upkey_arg = keyval; }
void tetrinet_changeteam (const char *t G_GNUC_UNUSED) {}
void tetrinet_redrawfields (void) {}
void tetrinet_playerline (const char *text G_GNUC_UNUSED) {}

int client_init_calls;
void client_init (const char *s G_GNUC_UNUSED, const char *n G_GNUC_UNUSED) { client_init_calls++; }
int client_disconnect_calls;
void client_disconnect (void) { client_disconnect_calls++; }
void client_poll_connect (void) {}
void client_poll_socket (void) {}
int client_outmessage_calls;
enum outmsg_type client_outmessage_last_type;
char client_outmessage_last_str[512];
void client_outmessage (enum outmsg_type msgtype, char *str)
{
    client_outmessage_calls++;
    client_outmessage_last_type = msgtype;
    GTET_STRCPY (client_outmessage_last_str, str, sizeof (client_outmessage_last_str));
}

int fields_init (void) { return 0; }
void fields_cleanup (void) {}
void fields_page_new (void) {}
void fields_page_destroy_contents (void) {}
int fields_screen_width (void) { return 400; }
int fields_screen_height (void) { return 300; }
void fields_render (SDL_Surface *dst G_GNUC_UNUSED) {}
int fields_gmsginput_calls, fields_gmsginput_arg;
void fields_gmsginput (int i) { fields_gmsginput_calls++; fields_gmsginput_arg = i; }
int fields_gmsginputactivate_calls;
void fields_gmsginputactivate (int i G_GNUC_UNUSED) { fields_gmsginputactivate_calls++; }
int fields_gmsginputclear_calls;
void fields_gmsginputclear (void) { fields_gmsginputclear_calls++; }
static char gmsg_text_buf[512];
const char *fields_gmsginputtext (void) { return gmsg_text_buf; }
int fields_gmsg_textinput_calls;
void fields_gmsg_textinput (const char *text) { fields_gmsg_textinput_calls++; strcat (gmsg_text_buf, text); }
int fields_gmsg_backspace_calls;
void fields_gmsg_backspace (void) { fields_gmsg_backspace_calls++; }

int partyline_page_new_calls;
int partyline_page_new (void) { partyline_page_new_calls++; return 0; }
void partyline_render (SDL_Surface *dst G_GNUC_UNUSED, const SDL_Rect *rect G_GNUC_UNUSED) {}
int partyline_textinput_calls;
void partyline_textinput (const char *text G_GNUC_UNUSED) { partyline_textinput_calls++; }
int partyline_backspace_calls;
void partyline_backspace (void) { partyline_backspace_calls++; }
int partyline_keydown_calls, partyline_keydown_arg;
void partyline_keydown (int keycode) { partyline_keydown_calls++; partyline_keydown_arg = keycode; }
int partyline_entryfocus_calls;
void partyline_entryfocus (void) { partyline_entryfocus_calls++; }
void partyline_show_channel_list (int show G_GNUC_UNUSED) {}

int winlist_page_new (void) { return 0; }
void winlist_page_cleanup (void) {}
void winlist_render (SDL_Surface *dst G_GNUC_UNUSED, const SDL_Rect *rect G_GNUC_UNUSED) {}
int winlist_focus_calls;
void winlist_focus (void) { winlist_focus_calls++; }

int sound_init (void) { return 0; }
void sound_cleanup (void) {}
int soundenable;

void config_loadconfig (void) {}
void config_loadconfig_keys (void) {}
void config_loadconfig_themes (void) {}

int stub_dialog_open;
int dialog_is_open (void) { return stub_dialog_open; }
void dialog_render (SDL_Surface *dst G_GNUC_UNUSED, const SDL_Rect *rect G_GNUC_UNUSED) {}
int dialog_textinput_calls;
void dialog_textinput (const char *text G_GNUC_UNUSED) { dialog_textinput_calls++; }
int dialog_backspace_calls;
void dialog_backspace (void) { dialog_backspace_calls++; }
int dialog_keydown_calls, dialog_keydown_arg;
void dialog_keydown (int keycode) { dialog_keydown_calls++; dialog_keydown_arg = keycode; }
int dialog_click_calls;
void dialog_click (int x G_GNUC_UNUSED, int y G_GNUC_UNUSED) { dialog_click_calls++; }
void dialog_tick (void) {}

void commands_init (void) {}
void commands_checkstate (void) {}
static T_menuitem stub_menu_items[1] = { { CMD_CONNECT, "Connect", 1, 1 } };
int commands_menu_count (void) { return 1; }
const T_menuitem *commands_menu_item (int index) { return index == 0 ? &stub_menu_items[0] : NULL; }
int commands_activate_calls;
T_command_id commands_activate_arg;
void commands_activate (T_command_id id) { commands_activate_calls++; commands_activate_arg = id; }

static int fails = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf ("FAIL: %s\n", msg); fails++; } \
    else printf ("ok: %s\n", msg); \
} while (0)

int main (void)
{
    win_w = 640; win_h = 480;
    keys[K_GAMEMSG] = SDLK_t;

    /* --- switch_page: no-op while disconnected --- */
    connected = 0;
    switch_page (PAGE_PARTYLINE);
    CHECK (partyline_entryfocus_calls == 0, "switch_page is a no-op while disconnected");
    CHECK (current_page == PAGE_PARTYLINE, "switch_page still updates current_page while disconnected");

    /* --- switch_page: connected, each page's original-matching behavior --- */
    connected = 1;
    gmsgstate = 0;
    switch_page (PAGE_FIELDS);
    CHECK (partyline_entryfocus_calls == 1,
           "switch_page(FIELDS) with gmsgstate==0 calls partyline_entryfocus (matches original's case-0 quirk)");

    gmsgstate = 1;
    switch_page (PAGE_FIELDS);
    CHECK (fields_gmsginputactivate_calls == 1,
           "switch_page(FIELDS) with gmsgstate==1 calls fields_gmsginputactivate instead");
    gmsgstate = 0;

    switch_page (PAGE_PARTYLINE);
    CHECK (partyline_entryfocus_calls == 2, "switch_page(PARTYLINE) calls partyline_entryfocus");

    switch_page (PAGE_WINLIST);
    CHECK (winlist_focus_calls == 1, "switch_page(WINLIST) calls winlist_focus");

    /* --- route_keydown: dialog priority over everything else --- */
    stub_dialog_open = 1;
    dialog_keydown_calls = dialog_backspace_calls = 0;
    route_keydown (SDLK_a);
    CHECK (dialog_keydown_calls == 1, "route_keydown forwards to dialog_keydown when a dialog is open");
    route_keydown (SDLK_BACKSPACE);
    CHECK (dialog_backspace_calls == 1, "route_keydown routes Backspace to dialog_backspace, not dialog_keydown");
    stub_dialog_open = 0;

    /* --- route_keydown: Alt+1/2/3 page switch --- */
    /* SDL_GetModState() reflects real modifier state, which we can't
       fake without a real event; route_keydown checks it directly, so
       this path is exercised implicitly by the live app run instead
       (see commit message) -- skip asserting it here. */

    /* --- route_keydown: fields page, normal key goes to tetrinet_key --- */
    current_page = PAGE_FIELDS;
    ingame = 0;
    tetrinet_key_calls = 0;
    route_keydown (SDLK_LEFT);
    CHECK (tetrinet_key_calls == 1 && tetrinet_key_arg == SDLK_LEFT,
           "route_keydown on the fields page forwards ordinary keys to tetrinet_key");

    /* --- route_keydown: gmsg activation --- */
    ingame = 1;
    gmsgstate = 0;
    fields_gmsginputactivate_calls = 0;
    fields_gmsginput_calls = 0;
    route_keydown (SDLK_t); /* keys[K_GAMEMSG] */
    CHECK (gmsgstate == 1, "pressing the message key while ingame sets gmsgstate");
    CHECK (fields_gmsginputactivate_calls == 1 && fields_gmsginput_calls == 1 && fields_gmsginput_arg == 1,
           "activating gmsg calls fields_gmsginputactivate and fields_gmsginput(1)");

    /* --- route_keydown: while gmsgstate, keys go to the gmsg buffer, not tetrinet_key --- */
    tetrinet_key_calls = 0;
    route_keydown (SDLK_LEFT);
    CHECK (tetrinet_key_calls == 0, "while gmsgstate is set, game keys don't reach tetrinet_key");

    /* --- gmsg_submit: normal message --- */
    GTET_STRCPY (nick, "Alice", sizeof (nick));
    gmsg_text_buf[0] = 0;
    strcat (gmsg_text_buf, "hello there");
    client_outmessage_calls = 0;
    fields_gmsginputclear_calls = 0;
    route_keydown (SDLK_RETURN);
    CHECK (client_outmessage_calls == 1 && client_outmessage_last_type == OUT_GMSG &&
           strcmp (client_outmessage_last_str, "<Alice> hello there") == 0,
           "Enter while gmsgstate submits '<nick> text' via client_outmessage(OUT_GMSG, ...)");
    CHECK (gmsgstate == 0 && fields_gmsginputclear_calls == 1,
           "submitting clears gmsgstate and the input buffer");

    /* --- gmsg_submit: /me formatting --- */
    gmsgstate = 1;
    gmsg_text_buf[0] = 0;
    strcat (gmsg_text_buf, "/me waves");
    client_outmessage_calls = 0;
    route_keydown (SDLK_RETURN);
    CHECK (client_outmessage_calls == 1 && strcmp (client_outmessage_last_str, "* Alice waves") == 0,
           "'/me <action>' submits '* nick action' instead of '<nick> text'");

    /* --- gmsg_submit: empty text sends nothing but still clears --- */
    gmsgstate = 1;
    gmsg_text_buf[0] = 0;
    client_outmessage_calls = 0;
    route_keydown (SDLK_RETURN);
    CHECK (client_outmessage_calls == 0, "submitting empty text sends no message");
    CHECK (gmsgstate == 0, "but still exits gmsg mode");

    /* --- route_keydown: gmsg backspace routes to fields_gmsg_backspace --- */
    ingame = 1; gmsgstate = 1;
    fields_gmsg_backspace_calls = 0;
    route_keydown (SDLK_BACKSPACE);
    CHECK (fields_gmsg_backspace_calls == 1, "Backspace during gmsg input routes to fields_gmsg_backspace");
    gmsgstate = 0;

    /* --- route_keydown: partyline page --- */
    current_page = PAGE_PARTYLINE;
    partyline_backspace_calls = 0;
    route_keydown (SDLK_BACKSPACE);
    CHECK (partyline_backspace_calls == 1, "Backspace on the partyline page routes to partyline_backspace");
    partyline_keydown_calls = 0;
    route_keydown (SDLK_RETURN);
    CHECK (partyline_keydown_calls == 1 && partyline_keydown_arg == SDLK_RETURN,
           "other keys on the partyline page route to partyline_keydown");

    /* --- route_keyup: only fires tetrinet_upkey on the fields page, not during gmsg, not with a dialog open --- */
    current_page = PAGE_FIELDS;
    gmsgstate = 0;
    stub_dialog_open = 0;
    tetrinet_upkey_calls = 0;
    route_keyup (SDLK_LEFT);
    CHECK (tetrinet_upkey_calls == 1, "route_keyup forwards to tetrinet_upkey on the fields page");

    gmsgstate = 1;
    tetrinet_upkey_calls = 0;
    route_keyup (SDLK_LEFT);
    CHECK (tetrinet_upkey_calls == 0, "route_keyup is suppressed while gmsgstate is set");
    gmsgstate = 0;

    stub_dialog_open = 1;
    tetrinet_upkey_calls = 0;
    route_keyup (SDLK_LEFT);
    CHECK (tetrinet_upkey_calls == 0, "route_keyup is suppressed while a dialog is open");
    stub_dialog_open = 0;

    /* --- route_textinput --- */
    current_page = PAGE_FIELDS;
    gmsgstate = 1;
    fields_gmsg_textinput_calls = 0;
    route_textinput ("x");
    CHECK (fields_gmsg_textinput_calls == 1, "route_textinput goes to fields_gmsg_textinput during gmsg entry");
    gmsgstate = 0;

    current_page = PAGE_PARTYLINE;
    partyline_textinput_calls = 0;
    route_textinput ("x");
    CHECK (partyline_textinput_calls == 1, "route_textinput goes to partyline_textinput on the partyline page");

    stub_dialog_open = 1;
    dialog_textinput_calls = 0;
    route_textinput ("x");
    CHECK (dialog_textinput_calls == 1, "route_textinput is captured by an open dialog first");
    stub_dialog_open = 0;

    /* --- route_click: menu bar hit-test --- */
    menu_rect_count = 1;
    menu_rects[0].x = 10; menu_rects[0].y = 10; menu_rects[0].w = 50; menu_rects[0].h = 20;
    menu_rect_ids[0] = CMD_CONNECT;
    commands_activate_calls = 0;
    route_click (15, 15);
    CHECK (commands_activate_calls == 1 && commands_activate_arg == CMD_CONNECT,
           "clicking inside a cached menu rect calls commands_activate with its id");

    /* --- route_click: tab bar hit-test --- */
    tab_rects[PAGE_WINLIST].x = 400; tab_rects[PAGE_WINLIST].y = 30;
    tab_rects[PAGE_WINLIST].w = 100; tab_rects[PAGE_WINLIST].h = 20;
    connected = 0; /* keep switch_page's side effects out of this assertion */
    route_click (420, 35);
    CHECK (current_page == PAGE_WINLIST, "clicking inside a tab rect switches to that page");

    /* --- route_click: dialog priority --- */
    stub_dialog_open = 1;
    dialog_click_calls = 0;
    route_click (1, 1);
    CHECK (dialog_click_calls == 1, "route_click is captured by an open dialog first");

    printf ("%s\n", fails ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED");
    return fails ? 1 : 0;
}
