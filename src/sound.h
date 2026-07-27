#define S_NUM 10

#define S_DROP        0
#define S_SOLIDIFY    1
#define S_LINECLEAR   2
#define S_TETRIS      3
#define S_ROTATE      4
#define S_SPECIALLINE 5
#define S_YOUWIN      6
#define S_YOULOSE     7
#define S_MESSAGE     8
#define S_GAMESTART   9

extern int soundenable;

extern char soundfiles[S_NUM][1024];

/* Theme's background music track (e.g. the classic TetriNET MIDI theme),
 * set by config_loadtheme() from theme.cfg's [Sounds] "Theme" key --
 * empty string if the theme doesn't ship one. */
extern char musicfile[1024];

/* Call once at startup, after SDL_Init(SDL_INIT_AUDIO) -- opens the
 * audio device via SDL_mixer. Returns 0 on success. If this fails (no
 * audio hardware, e.g. some IRIX/O2 configurations), sound_cache()/
 * sound_playsound() remain safe, silent no-ops rather than crashing --
 * matches how the original HAVE_CANBERRAGTK-less build already had a
 * silent no-op fallback for "no sound support compiled in" at all. */
int sound_init (void);
void sound_cleanup (void);

void sound_cache (void);
void sound_playsound (int id);

/* Starts/stops the theme's looping background music (tetrinet_startgame()/
 * tetrinet_endgame() call these). Safe no-ops if sound is disabled, the
 * mixer failed to open, musicfile is empty, or this SDL_mixer build has
 * no MIDI backend (some SDL_mixer 1.2 builds on IRIX/PPC-Tiger don't --
 * Mix_LoadMUS() just returns NULL there, handled the same as an
 * unplayable sound effect elsewhere in this file). */
void sound_playmusic (void);
void sound_stopmusic (void);

