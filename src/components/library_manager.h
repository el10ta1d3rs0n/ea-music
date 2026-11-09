#ifndef LIBRARY_MANAGER_H
#define LIBRARY_MANAGER_H

#include <stdbool.h>

#ifndef MAX_LIBRARY_SONGS
#define MAX_LIBRARY_SONGS 512
#endif

#ifndef SONG_TITLE_SIZE
#define SONG_TITLE_SIZE 256
#endif

#ifndef SONG_ID_SIZE
#define SONG_ID_SIZE 64
#endif

typedef struct {
    char title[SONG_TITLE_SIZE];
    char id[SONG_ID_SIZE];
    char duration[16];
} LibrarySong;

/* Load persisted settings. Call once from UIInit. */
void LibraryManagerInit(void);

void LibraryManagerOpen(const char *libName);
bool LibraryManagerIsOpen(void);
void LibraryManagerClose(void);
const char *LibraryManagerGetCurrentLib(void);

void LibraryManagerUpdate(void);
void LibraryManagerDraw(int screenWidth, int screenHeight);

/* Call every frame from UIUpdate, before any modal. */
void LibraryManagerTick(void);

/* Persisted Repeat-All setting. */
bool LibraryManagerGetRepeatAll(void);
void LibraryManagerSetRepeatAll(bool on);

/*
 * Clear the “playing from library” context when playback starts or stops
 * outside the library (for example, from Search, the Stop button, or the
 * S shortcut).
 */
void LibraryManagerClearPlaybackSource(void);

#endif /* LIBRARY_MANAGER_H */

