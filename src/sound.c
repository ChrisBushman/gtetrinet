/*
 *  GTetrinet
 *  Copyright (C) 1999, 2000, 2001, 2002, 2003  Ka-shu Wong (kswong@zip.com.au)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <stdlib.h>
#include <stdio.h>
#include <SDL_mixer.h>

#include "sound.h"

int soundenable;

char soundfiles[S_NUM][1024];

static int mixer_ready = 0;
static Mix_Chunk *samples[S_NUM] = {NULL};

int sound_init (void)
{
    if (Mix_OpenAudio (44100, MIX_DEFAULT_FORMAT, 2, 1024) != 0) {
        fprintf (stderr, "sound_init: Mix_OpenAudio failed: %s (sound disabled)\n", Mix_GetError ());
        mixer_ready = 0;
        return -1;
    }
    mixer_ready = 1;
    return 0;
}

void sound_cleanup (void)
{
    int i;

    for (i = 0; i < S_NUM; i++) {
        if (samples[i] != NULL) {
            Mix_FreeChunk (samples[i]);
            samples[i] = NULL;
        }
    }
    if (mixer_ready) {
        Mix_CloseAudio ();
        mixer_ready = 0;
    }
}

void sound_cache (void)
{
    int i;

    if (!soundenable || !mixer_ready)
        return;

    for (i = 0; i < S_NUM; i++) {
        if (samples[i] != NULL) {
            Mix_FreeChunk (samples[i]);
            samples[i] = NULL;
        }
        if (soundfiles[i][0])
            samples[i] = Mix_LoadWAV (soundfiles[i]);
        /* A theme that doesn't ship this particular sound (soundfiles[i]
         * left empty by config_loadtheme) or a file SDL_mixer can't
         * decode both just leave samples[i] NULL -- sound_playsound()
         * below already treats that as "nothing to play", matching the
         * original's silent handling of an unset sound slot. */
    }
}

void sound_playsound (int id)
{
    if (!soundenable || !mixer_ready)
        return;
    if (samples[id] != NULL)
        Mix_PlayChannel (-1, samples[id], 0);
}
