#ifndef EA_MUSIC_LIBRARY_DIALOG_H
#define EA_MUSIC_LIBRARY_DIALOG_H

#include <stdbool.h>
#include <stddef.h>

#define EA_LIBRARY_NAME_SIZE 64

void LibraryDialogOpen(void);
bool LibraryDialogIsOpen(void);
bool LibraryDialogUpdate(char *createdName, size_t capacity);
void LibraryDialogDraw(void);

/* Scans the config folder for *.lib files and fills `libraries`
 * with one entry per file (extension stripped). Returns the count
 * (capped at `maxLibraries`). Each library lives in its own file,
 * so no aggregator file / no shared library folder is involved. */
int LibraryDialogLoadAll(char libraries[][EA_LIBRARY_NAME_SIZE],
                         int maxLibraries);

#endif
