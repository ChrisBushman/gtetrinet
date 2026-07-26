extern char blocksfile[1024];
extern int bsize;
extern GString *currenttheme;

/* SDL_Keycode (SDL2) / SDLKey (SDL 1.2) values -- both are plain
 * integer-compatible types, so "int" here works unmodified against
 * either SDL version's headers, matching how tetrinet_key()/
 * tetrinet_upkey() already take a plain "int keyval" rather than a
 * GDK-specific type. GDK's case-insensitive gdk_keyval_to_lower()
 * comparison this replaced is not needed for SDL keycodes: unlike GDK
 * keyvals (where Shift+a and a are different keyval numbers), SDL
 * keycodes for letters are already shift-independent -- SDLK_a means
 * "the A key was pressed", with Shift reported separately as a
 * modifier, not folded into a different keycode. */
extern int keys[];
extern int defaultkeys[];

extern void config_loadtheme (const gchar *themedir);
extern int config_getthemeinfo (char *themedir, char *name, char *author, char *desc);
extern void config_loadconfig (void);
extern void config_loadconfig_keys (void);
extern void config_loadconfig_themes (void);

#define GTETRINET_THEMES GTETRINET_DATA"/themes"
#define DEFAULTTHEME GTETRINET_THEMES"/default/"

typedef enum
{
  K_RIGHT, 
  K_LEFT, 
  K_ROTRIGHT, 
  K_ROTLEFT,
  K_DOWN,
  K_DROP,
  K_DISCARD,
  K_GAMEMSG,
  K_SPECIAL1,
  K_SPECIAL2,
  K_SPECIAL3, 
  K_SPECIAL4,
  K_SPECIAL5,
  K_SPECIAL6,
/* not a key but the number of configurable keys */
  K_NUM
} GTetrinetKeys;
