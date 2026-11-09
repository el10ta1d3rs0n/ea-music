#ifndef YTDLP_H
#define YTDLP_H

#include <stdbool.h>
#include "raylib.h"

#define MAX_RESULTS 20

#define TITLE_LEN    256
#define ID_LEN       64
#define DURATION_LEN 16
#define URL_LEN      512

#define YTDLP_VOLUME_MAX     200
#define YTDLP_VOLUME_DEFAULT 120

typedef struct {
    char id[ID_LEN];
    char title[TITLE_LEN];
    char duration[DURATION_LEN];

    char thumbnail_url[URL_LEN];
    char local_thumb_path[URL_LEN];
    Texture2D thumbnail_texture;
    bool thumb_loaded;
} SongResult;

void YTDLP_Init(void);
void YTDLP_Cleanup(void);

/* Search */
void YTDLP_SearchAsync(const char *query);
void YTDLP_ClearResults(void);
int  YTDLP_GetResults(SongResult *out_results, int max_out);
bool YTDLP_IsSearching(void);

/* Playback */
void YTDLP_PlaySong(const char *id);
void YTDLP_StopPlayback(void);
bool YTDLP_IsPlaying(void);

double YTDLP_GetTime(void);
double YTDLP_GetDuration(void);
void   YTDLP_Seek(double seconds);

void YTDLP_TogglePause(void);
bool YTDLP_IsPaused(void);

/* Repeat the current song (not saved between runs) */
void YTDLP_SetLoop(bool enabled);
bool YTDLP_GetLoop(void);

/* Volume: 0..YTDLP_VOLUME_MAX, starts at YTDLP_VOLUME_DEFAULT */
void YTDLP_SetVolume(int percent);
int  YTDLP_GetVolume(void);

/* Audio normalization (off by default, saved between runs) */
void YTDLP_SetNormalize(bool enabled);
bool YTDLP_GetNormalize(void);

#endif /* YTDLP_H */
