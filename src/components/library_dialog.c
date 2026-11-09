#include "library_dialog.h"
#include "../ui.h"

#include "raylib.h"
#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#define MKDIR_ONE(path)  mkdir(path, 0755)
#define PATH_SEP_STR     "/"

#define NAME_SIZE  EA_LIBRARY_NAME_SIZE
#define LIB_EXT    ".lib"

/* Palette mirrored from ui.c so the dialog feels native. */
static const Color DLG_WHITE    = { 239, 241, 247, 255 };
static const Color DLG_MUTED    = { 151, 158, 173, 255 };
static const Color DLG_ACCENT   = { 161, 143, 255, 255 };
static const Color DLG_PANEL    = {  26,  29,  37, 255 };
static const Color DLG_BG       = {  18,  20,  26, 255 };
static const Color DLG_BORDER   = {  47,  51,  62, 255 };
static const Color DLG_DARK     = {  24,  22,  31, 255 };
static const Color DLG_HOVER    = {  52,  56,  68, 255 };
static const Color DLG_CANCEL   = {  43,  47,  58, 255 };
static const Color DLG_DISABLED = {  52,  56,  68, 255 };
static const Color DLG_ERROR    = { 232, 128, 128, 255 };

static bool open = false;
static char name[NAME_SIZE];
static char error[160];

/* ---------------- Text helpers (match ui.c) ---------------- */

static void DialogText(const char *text, Vector2 position,
                       float size, Color color)
{
    position.x = roundf(position.x);
    position.y = roundf(position.y);
    DrawTextEx(UIGetFontSized(size), text, position, size, 0.0f, color);
}

static float DialogTextWidth(const char *text, float size)
{
    return MeasureTextEx(UIGetFontSized(size), text, size, 0.0f).x;
}

/* ---------------- Config folder (one file per library) ---------------- */

static bool CreateDirRecursive(const char *path)
{
    char buffer[512];
    size_t len = strlen(path);
    if (len == 0 || len >= sizeof(buffer)) return false;
    memcpy(buffer, path, len + 1);

    for (size_t i = 1; i < len; i++) {
        if (buffer[i] == '/') {
            char saved = buffer[i];
            buffer[i] = '\0';
            MKDIR_ONE(buffer);
            buffer[i] = saved;
        }
    }

    if (MKDIR_ONE(buffer) == 0) return true;
    return errno == EEXIST;
}

static bool GetConfigDir(char *out, size_t outSize)
{
    const char *xdg  = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    if (xdg && xdg[0])
        snprintf(out, outSize, "%s/ea-music", xdg);
    else if (home && home[0])
        snprintf(out, outSize, "%s/.config/ea-music", home);
    else
        return false;

    CreateDirRecursive(out);
    return true;
}

static void TrimTrailing(char *s)
{
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == ' '  ||
                       s[len - 1] == '\t' ||
                       s[len - 1] == '.'))
        s[--len] = '\0';
}

static bool GetLibraryPath(const char *libName, char *out, size_t outSize)
{
    char dir[512];
    if (!GetConfigDir(dir, sizeof(dir))) return false;

    int n = snprintf(out, outSize, "%s" PATH_SEP_STR "%.*s%s",
                     dir, NAME_SIZE - 1, libName, LIB_EXT);

    return n > 0 && (size_t)n < outSize;
}

static bool CreateLibraryFile(const char *libName)
{
    char path[1024];
    if (!GetLibraryPath(libName, path, sizeof(path))) return false;
    if (FileExists(path)) return false;

    FILE *f = fopen(path, "wb");
    if (!f) return false;
    fclose(f);
    return true;
}

int LibraryDialogLoadAll(char libraries[][EA_LIBRARY_NAME_SIZE],
                         int maxLibraries)
{
    if (!libraries || maxLibraries <= 0) return 0;

    char dir[512];
    if (!GetConfigDir(dir, sizeof(dir))) return 0;
    if (!DirectoryExists(dir)) return 0;

    char tempNames[128][EA_LIBRARY_NAME_SIZE];
    int tempCount = 0;

    DIR *d = opendir(dir);
    if (d) {
        struct dirent *entry;
        while ((entry = readdir(d)) != NULL && tempCount < 128) {
            const char *fname = entry->d_name;
            size_t len = strlen(fname);
            
            /* Match files ending in .lib */
            if (len > 4 && strcmp(fname + len - 4, LIB_EXT) == 0) {
                snprintf(tempNames[tempCount], EA_LIBRARY_NAME_SIZE, "%.*s", (int)(len - 4), fname);
                tempCount++;
            }
        }
        closedir(d);
    }

    /* Sort alphabetically so ordering is stable across filesystems. */
    for (int i = 0; i < tempCount - 1; i++) {
        for (int j = 0; j < tempCount - i - 1; j++) {
            if (strcmp(tempNames[j], tempNames[j + 1]) > 0) {
                char tmp[EA_LIBRARY_NAME_SIZE];
                memcpy(tmp, tempNames[j], EA_LIBRARY_NAME_SIZE);
                memcpy(tempNames[j], tempNames[j + 1], EA_LIBRARY_NAME_SIZE);
                memcpy(tempNames[j + 1], tmp, EA_LIBRARY_NAME_SIZE);
            }
        }
    }

    int count = 0;
    for (int i = 0; i < tempCount && count < maxLibraries; i++) {
        size_t len = strlen(tempNames[i]);
        if (len >= EA_LIBRARY_NAME_SIZE) len = EA_LIBRARY_NAME_SIZE - 1;
        memcpy(libraries[count], tempNames[i], len);
        libraries[count][len] = '\0';
        count++;
    }

    return count;
}

/* ---------------- Input validation ---------------- */

static bool IsAllowedChar(int c)
{
    if (c < 32 || c > 125) return false;
    switch (c) {
        case '/': case '\\': case ':': case '*':
        case '?': case '"':  case '<': case '>': case '|':
            return false;
        default:
            return true;
    }
}

static bool TryCreateLibrary(char *createdName, size_t capacity)
{
    char trimmed[NAME_SIZE];
    snprintf(trimmed, sizeof(trimmed), "%s", name);
    TrimTrailing(trimmed);

    if (!trimmed[0]) {
        snprintf(error, sizeof(error), "Please enter a name.");
        return false;
    }

    if (!CreateLibraryFile(trimmed)) {
        snprintf(error, sizeof(error),
                 "A library named \"%s\" already exists.", trimmed);
        return false;
    }

    if (createdName && capacity > 0) {
        strncpy(createdName, trimmed, capacity - 1);
        createdName[capacity - 1] = '\0';
    }

    return true;
}

/* ---------------- Dialog API ---------------- */

void LibraryDialogOpen(void)
{
    open = true;
    name[0]  = '\0';
    error[0] = '\0';
}

bool LibraryDialogIsOpen(void)
{
    return open;
}

bool LibraryDialogUpdate(char *createdName, size_t capacity)
{
    if (!open) return false;

    bool controlDown = IsKeyDown(KEY_LEFT_CONTROL) ||
                       IsKeyDown(KEY_RIGHT_CONTROL);

    if (controlDown && IsKeyPressed(KEY_A)) {
        name[0]  = '\0';
        error[0] = '\0';
    }

    int character = GetCharPressed();
    while (character > 0) {
        int length = (int)strlen(name);
        if (IsAllowedChar(character) && length < NAME_SIZE - 1) {
            name[length]     = (char)character;
            name[length + 1] = '\0';
            error[0] = '\0';
        }
        character = GetCharPressed();
    }

    if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE) ||
        IsKeyPressed(KEY_DELETE)    || IsKeyPressedRepeat(KEY_DELETE)) {
        int length = (int)strlen(name);
        if (length > 0) name[length - 1] = '\0';
        error[0] = '\0';
    }

    /* Esc exits without creating anything. */
    if (IsKeyPressed(KEY_ESCAPE)) {
        open = false;
        return false;
    }

    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    Vector2 mouse = GetMousePosition();
    Rectangle dialog = { (sw - 420.0f) / 2, (sh - 230.0f) / 2, 420, 230 };
    Rectangle cancel = { dialog.x + 170, dialog.y + 164, 100, 42 };
    Rectangle create = { dialog.x + 278, dialog.y + 164, 112, 42 };

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        if (CheckCollisionPointRec(mouse, cancel)) {
            open = false;
            return false;
        }
        if (CheckCollisionPointRec(mouse, create) && name[0]) {
            if (TryCreateLibrary(createdName, capacity)) {
                open = false;
                return true;
            }
            return false; /* keep dialog open, show error */
        }
    }

    /* Enter (or numpad Enter) creates the library file. */
    if ((IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) && name[0]) {
        if (TryCreateLibrary(createdName, capacity)) {
            open = false;
            return true;
        }
        return false;
    }

    return false;
}

void LibraryDialogDraw(void)
{
    if (!open) return;

    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    Rectangle dialog = { (sw - 420.0f) / 2, (sh - 230.0f) / 2, 420, 230 };
    Rectangle input  = { dialog.x + 28,  dialog.y +  82, 364, 48 };
    Rectangle cancel = { dialog.x + 170, dialog.y + 164, 100, 42 };
    Rectangle create = { dialog.x + 278, dialog.y + 164, 112, 42 };
    Vector2 mouse = GetMousePosition();

    /* Dim the whole screen, same pattern as the menu overlay. */
    DrawRectangle(0, 0, sw, sh, (Color){ 0, 0, 0, 170 });

    /* Dialog panel */
    DrawRectangleRounded(dialog, 0.08f, 10, DLG_PANEL);

    /* Header */
    DialogText("Create a library",
               (Vector2){ dialog.x + 28, dialog.y + 26 }, 22, DLG_WHITE);
    DialogText("Choose a name for your library",
               (Vector2){ dialog.x + 28, dialog.y + 55 }, 14, DLG_MUTED);

    /* Input field */
    DrawRectangleRounded(input, 0.16f, 8, DLG_BG);
    DrawRectangleRoundedLines(input, 0.16f, 8, DLG_BORDER);

    bool  hasName = name[0] != '\0';
    float textX   = input.x + 14;

    if (hasName)
        DialogText(name, (Vector2){ textX, input.y + 15 }, 16, DLG_WHITE);
    else
        DialogText("Library name",
                   (Vector2){ textX, input.y + 15 }, 16, DLG_MUTED);

    /* Blinking cursor */
    if ((int)(GetTime() * 2) % 2 == 0) {
        float textW = hasName ? DialogTextWidth(name, 16) : 0.0f;
        DrawRectangle((int)(textX + textW), (int)input.y + 15,
                      2, 19, DLG_ACCENT);
    }

    /* Inline error (only shown after a failed create attempt) */
    if (error[0])
        DialogText(error,
                   (Vector2){ input.x, input.y + input.height + 8 },
                   12, DLG_ERROR);

    /* Cancel button */
    bool hoverCancel = CheckCollisionPointRec(mouse, cancel);
    DrawRectangleRounded(cancel, 0.2f, 8,
        hoverCancel ? DLG_HOVER : DLG_CANCEL);
    float cancelW = DialogTextWidth("Cancel", 15);
    DialogText("Cancel",
               (Vector2){ cancel.x + (cancel.width - cancelW) / 2.0f,
                          cancel.y + 13 },
               15, DLG_WHITE);

    /* Create button */
    Color createColor = hasName ? DLG_ACCENT : DLG_DISABLED;
    DrawRectangleRounded(create, 0.2f, 8, createColor);
    float createW = DialogTextWidth("Create", 15);
    DialogText("Create",
               (Vector2){ create.x + (create.width - createW) / 2.0f,
                          create.y + 13 },
               15, hasName ? DLG_DARK : DLG_MUTED);
}
