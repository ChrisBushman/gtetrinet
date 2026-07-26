#include <glib.h>
#include <SDL.h>

extern int randomnum (int n);
extern void fdreadline (int fd, char *buf);
extern char *nocolor (char *str);

/* Function to validate a char*, and if it's not utf-8, try the locale
 * or iso8859-1. The returned gchar* must be freed. */
gchar* ensure_utf8(const char* str);

/* Better versions of the std. string functions */
#define GTET_STRCPY(x, y, sz) G_STMT_START { \
  size_t gtet_strcpy_x_sz = (sz); \
  size_t gtet_strcpy_y_len = strlen(y); \
  \
  g_assert(gtet_strcpy_x_sz); \
  \
  if (gtet_strcpy_y_len >= gtet_strcpy_x_sz) \
    gtet_strcpy_y_len = gtet_strcpy_x_sz - 1; \
  \
  if (gtet_strcpy_y_len) \
    memcpy((x), (y), gtet_strcpy_y_len); \
  (x)[gtet_strcpy_y_len] = 0; \
 } G_STMT_END

#define GTET_STRCAT(x, y, sz) G_STMT_START { \
  size_t gtet_strcat_x_sz = (sz); \
  size_t gtet_strcat_x_len = strlen(x); \
  size_t gtet_strcat_y_len = strlen(y); \
  \
  g_assert(gtet_strcat_x_sz); \
  \
  if (gtet_strcat_x_len >= (gtet_strcat_x_sz - 1)) \
    gtet_strcat_y_len = 0; \
  \
  gtet_strcat_x_sz -= gtet_strcat_x_len;  \
  if (gtet_strcat_y_len >= gtet_strcat_x_sz) \
    gtet_strcat_y_len = gtet_strcat_x_sz - 1; \
  \
  if (gtet_strcat_y_len) \
    memcpy((x) + gtet_strcat_x_len, (y), gtet_strcat_y_len); \
  (x)[gtet_strcat_x_len + gtet_strcat_y_len] = 0; \
 } G_STMT_END

/* these assume you are passing an "object", Ie. sizeof() returns the true
 * size */
#define GTET_O_STRCPY(x, y) G_STMT_START { \
  g_assert(sizeof(x) > 4); GTET_STRCPY(x, y, sizeof(x)); \
 } G_STMT_END

#define GTET_O_STRCAT(x, y) G_STMT_START { \
  g_assert(sizeof(x) > 4); GTET_STRCAT(x, y, sizeof(x)); \
 } G_STMT_END

/* textbox codes ... */

/* UTF-8 won't use 0xFF, so this is ok.  - vidar
 */
#define TETRI_TB_RESET 0xFF

#define TETRI_TB_BOLD             2
#define TETRI_TB_ITALIC          22
#define TETRI_TB_UNDERLINE       31

#define TETRI_TB_C_BEG_OFFSET     1 /* in theory 1 and 2 are colors ...
                                     * however 2 == bold */

/* colors... see colors[] in misc.c */
#define TETRI_TB_C_CYAN           3
#define TETRI_TB_C_BLACK          4
#define TETRI_TB_C_BRIGHT_BLUE    5
#define TETRI_TB_C_GREY           6

#define TETRI_TB_C_MAGENTA        8

/* #define TETRI_TB_C_GREY       11 -- dup */
#define TETRI_TB_C_DARK_GREEN    12

#define TETRI_TB_C_BRIGHT_GREEN  14
#define TETRI_TB_C_LIGHT_GREY    15
#define TETRI_TB_C_DARK_RED      16
#define TETRI_TB_C_DARK_BLUE     17
#define TETRI_TB_C_BROWN         18
#define TETRI_TB_C_PURPLE        19
#define TETRI_TB_C_BRIGHT_RED    20
/* #define TETRI_TB_C_LIGHT_GREY 21 -- dup */

#define TETRI_TB_C_DARK_CYAN     23
#define TETRI_TB_C_WHITE         24
#define TETRI_TB_C_YELLOW        25

#define TETRI_TB_C_END_OFFSET    25 /* highest color value */
#define TETRI_TB_END_OFFSET      31 /* highest value - must be less than 32 */

/* --- SDL text rendering (replaces GtkTextView/GtkTextTag machinery) ---
 *
 * The chat/battle-log text protocol above (0xFF-prefixed control bytes
 * for reset/bold/italic/underline/color) is presentation-agnostic and
 * kept as-is -- only how it's *consumed* changes. Instead of inserting
 * into a live GtkTextBuffer, misc_parse_formatted() walks a string and
 * invokes a callback once per contiguous same-styled run, so a caller
 * (fields.c's attack/defense log and in-game chat, later partyline.c's
 * chat window) can store the runs in its own plain-data scrollback
 * buffer and render them itself each frame -- matching SDL's
 * redraw-everything-every-frame model rather than GTK's retained-widget
 * one. */

typedef struct {
    SDL_Color color;
    int bold;
    int italic;
    int underline;
} T_textstyle;

/* Called once per contiguous run of identically-styled text (a run never
 * spans a style/color change or the string's end). text/len describe a
 * UTF-8 substring that is NOT nul-terminated on its own -- use len, not
 * strlen(). Never called with len == 0. */
typedef void (*T_formattedrun_fn) (const T_textstyle *style, const char *text, size_t len, void *userdata);

extern void misc_parse_formatted (const char *str, T_formattedrun_fn emit, void *userdata);

/* Load the bundled font once at startup (call after SDL_Init). Returns 0
 * on success. size_px is the point size for the single loaded face --
 * bold/italic/underline are synthesized from it via TTF_SetFontStyle
 * rather than needing separate bold/italic font files. */
extern int misc_font_init (const char *font_path, int size_px);
extern void misc_font_cleanup (void);

/* Render one styled run at (x, y) (top-left of the text). Returns the
 * pixel width consumed, so callers can lay out consecutive runs on the
 * same line left-to-right (matching how a GtkTextView would have flowed
 * consecutive insert_with_tags() calls). text/len as in T_formattedrun_fn. */
extern int misc_font_render (SDL_Surface *dst, int x, int y, const T_textstyle *style, const char *text, size_t len);

/* Height in pixels of a line of text at the loaded font's size -- callers
 * use this to lay out successive lines in a scrollback buffer. */
extern int misc_font_line_height (void);

/* --- Scrolling formatted-text log: shared by fields.c's attack/defense
 * log + in-game chat, and partyline.c's chat window. Originally each
 * caller's own private ring buffer of {style, text} runs per line
 * (matching the runs misc_parse_formatted() emits); pulled up into
 * misc.c once a second consumer (partyline.c) needed the exact same
 * thing, rather than duplicating the ring-buffer logic a second time. */

#define MISC_TEXTLOG_MAXLINES 256
#define MISC_TEXTLOG_RUNS_PER_LINE 16
#define MISC_TEXTLOG_RUN_TEXT_MAX 256

typedef struct {
    T_textstyle style;
    char text[MISC_TEXTLOG_RUN_TEXT_MAX];
} T_logrun;

typedef struct {
    T_logrun runs[MISC_TEXTLOG_RUNS_PER_LINE];
    int runcount;
} T_logline;

typedef struct {
    T_logline lines[MISC_TEXTLOG_MAXLINES];
    int count; /* number of valid lines, capped at MISC_TEXTLOG_MAXLINES */
    int next;  /* ring-buffer write position */
} T_textlog;

/* Parses str via misc_parse_formatted() and appends it as one new line
 * (the ring buffer's oldest line is silently overwritten once full,
 * same as a real chat window scrolling old lines away). */
extern void misc_textlog_append (T_textlog *log, const char *str);
extern void misc_textlog_clear (T_textlog *log);

/* Draws as many of the most recent lines as fit in rect's height,
 * newest line at the bottom (a chat window, not a top-down log). */
extern void misc_textlog_render (SDL_Surface *dst, const SDL_Rect *rect, const T_textlog *log);
