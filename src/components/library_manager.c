#include "library_manager.h"
#include "../ui.h"
#include "../backend/ytdlp.h"
#include "raylib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define MAKE_DIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#include <sys/types.h>
#define MAKE_DIR(p) mkdir((p), 0755)
#endif

static bool  isOpen           = false;
static char  currentLibrary[64];
static LibrarySong songs[MAX_LIBRARY_SONGS];
static int   songCount        = 0;
static int   selectedSong     = 0;
static float scrollOffset     = 0.0f;
static bool  repeatAll        = false;

/* ---------------- Playback-source state ---------------- */

/* Index of the song we last started from *this* library. -1 = none. */
static int  playingIndex = -1;

/* Which library the current playback came from ("" = not from a library). */
static char playingLibrary[64] = "";

/* The id we actually kicked off.  Used to re-find the song after reloads. */
static char playingSongId[SONG_ID_SIZE] = "";

/* True once YTDLP has actually reported IsPlaying()==true for this song. */
static bool wasPlaying = false;

/* =========================================================
   Paths / settings
   ========================================================= */

static bool GetLibraryFilePath(const char *libName, char *out, size_t outSize)
{
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && xdg[0]) {
        snprintf(out, outSize, "%s/ea-music/%s.lib", xdg, libName);
        return true;
    }
    const char *home = getenv("HOME");
    if (!home || !home[0]) return false;
    snprintf(out, outSize, "%s/.config/ea-music/%s.lib", home, libName);
    return true;
}

static bool GetConfigDir(char *out, size_t outSize)
{
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && xdg[0]) {
        snprintf(out, outSize, "%s/ea-music", xdg);
        return true;
    }
    const char *home = getenv("HOME");
    if (!home || !home[0]) return false;
    snprintf(out, outSize, "%s/.config/ea-music", home);
    return true;
}

/* ---------------- Settings paths ----------------
 *
 * ytdlp.c owns ~/.config/ea-music/settings.conf (volume, normalize) and
 * rewrites it with fopen(..., "w") on every save — including on exit
 * via YTDLP_Cleanup(). If we also wrote repeat_all there, ytdlp's next
 * save would truncate the file and erase our key. So Repeat-All lives
 * in its own file: ~/.config/ea-music/library.conf.
 *
 * The legacy shared path is still read ONCE as a migration source, so
 * users upgrading from an older build keep their Repeat-All setting.
 */

static bool GetLibraryConfigPath(char *out, size_t outSize)
{
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && xdg[0]) {
        int n = snprintf(out, outSize, "%s/ea-music/library.conf", xdg);
        return n > 0 && (size_t)n < outSize;
    }
    const char *home = getenv("HOME");
    if (!home || !home[0]) return false;
    int n = snprintf(out, outSize, "%s/.config/ea-music/library.conf", home);
    return n > 0 && (size_t)n < outSize;
}

/* Legacy shared file (now owned by ytdlp.c). Only read for migration. */
static bool GetLegacySettingsPath(char *out, size_t outSize)
{
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && xdg[0]) {
        int n = snprintf(out, outSize, "%s/ea-music/settings.conf", xdg);
        return n > 0 && (size_t)n < outSize;
    }
    const char *home = getenv("HOME");
    if (!home || !home[0]) return false;
    int n = snprintf(out, outSize, "%s/.config/ea-music/settings.conf", home);
    return n > 0 && (size_t)n < outSize;
}

static void MakeDirs(const char *path)
{
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; ++p) {
        if (*p == '/') {
            *p = '\0';
            MAKE_DIR(tmp);
            *p = '/';
        }
    }
    MAKE_DIR(tmp);
}

static void EnsureConfigDir(void)
{
    char dir[512];
    if (GetConfigDir(dir, sizeof(dir)))
        MakeDirs(dir);
}

/* Returns 1/0 if "repeat_all" is present in `path`, -1 otherwise. */
static int ReadRepeatAllKey(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;

    int result = -1;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *val = eq + 1;
        char *nl = strpbrk(val, "\r\n");
        if (nl) *nl = '\0';

        if (strcmp(line, "repeat_all") == 0)
            result = (strcmp(val, "1")    == 0 ||
                      strcmp(val, "true") == 0 ||
                      strcmp(val, "on")   == 0) ? 1 : 0;
    }
    fclose(f);
    return result;
}

static void SaveSettings(void)
{
    EnsureConfigDir();

    char path[512];
    if (!GetLibraryConfigPath(path, sizeof(path))) return;

    FILE *f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "repeat_all=%d\n", repeatAll ? 1 : 0);
    fclose(f);
}

static void LoadSettings(void)
{
    char path[512];

    /* 1. Our own file wins if it already exists. */
    if (GetLibraryConfigPath(path, sizeof(path))) {
        int v = ReadRepeatAllKey(path);
        if (v >= 0) { repeatAll = (v != 0); return; }
    }

    /* 2. Migration: read the legacy shared file once, then immediately
          persist to our own file so ytdlp.c can no longer stomp it. */
    if (GetLegacySettingsPath(path, sizeof(path))) {
        int v = ReadRepeatAllKey(path);
        if (v >= 0) {
            repeatAll = (v != 0);
            SaveSettings();
        }
    }
}

/* =========================================================
   Public API
   ========================================================= */

void LibraryManagerInit(void)
{
    LoadSettings();
}

bool LibraryManagerGetRepeatAll(void) { return repeatAll; }

void LibraryManagerSetRepeatAll(bool on)
{
    if (on == repeatAll) return;
    repeatAll = on;
    SaveSettings();
}

void LibraryManagerClearPlaybackSource(void)
{
    playingLibrary[0] = '\0';
    playingSongId[0]  = '\0';
    playingIndex      = -1;
    wasPlaying        = false;
}

/* =========================================================
   Library file IO
   ========================================================= */

static int LoadLibraryFile(const char *libName, LibrarySong *out, int maxSongs)
{
    char path[512];
    if (!GetLibraryFilePath(libName, path, sizeof(path))) return 0;

    FILE *f = fopen(path, "r");
    if (!f) return 0;

    int n = 0;
    char line[512];
    while (fgets(line, sizeof(line), f) && n < maxSongs) {
        char *title = strtok(line, "|");
        char *id    = strtok(NULL, "|");
        char *dur   = strtok(NULL, "\n");
        if (title && id) {
            snprintf(out[n].title,    SONG_TITLE_SIZE, "%s", title);
            snprintf(out[n].id,       SONG_ID_SIZE,    "%s", id);
            snprintf(out[n].duration, 16,              "%s", dur ? dur : "--:--");
            ++n;
        }
    }
    fclose(f);
    return n;
}

static void SaveLibrarySongs(void)
{
    char path[512];
    if (!GetLibraryFilePath(currentLibrary, path, sizeof(path))) return;

    FILE *f = fopen(path, "w");
    if (!f) return;
    for (int i = 0; i < songCount; ++i)
        fprintf(f, "%s|%s|%s\n", songs[i].title, songs[i].id, songs[i].duration);
    fclose(f);
}

/* =========================================================
   Open / close
   ========================================================= */

void LibraryManagerOpen(const char *libName)
{
    snprintf(currentLibrary, sizeof(currentLibrary), "%s", libName);
    isOpen        = true;
    selectedSong  = 0;
    scrollOffset  = 0.0f;

    songCount = LoadLibraryFile(libName, songs, MAX_LIBRARY_SONGS);

    /* If we're already playing from this library, keep the selection on it. */
    if (strcmp(playingLibrary, libName) == 0 &&
        playingIndex >= 0 && playingIndex < songCount) {
        selectedSong = playingIndex;
    }
}

bool LibraryManagerIsOpen(void)             { return isOpen; }
void LibraryManagerClose(void)              { isOpen = false; }
const char *LibraryManagerGetCurrentLib(void) { return currentLibrary; }

/* =========================================================
   Play / advance helpers
   ========================================================= */

static void PlaySongAt(int i)
{
    if (i < 0 || i >= songCount) return;

    snprintf(playingLibrary, sizeof(playingLibrary), "%s", currentLibrary);
    playingIndex = i;
    snprintf(playingSongId, sizeof(playingSongId), "%s", songs[i].id);

    /* Wait until the backend actually reports playing before we consider
       "wasPlaying" true; this prevents a startup flicker from auto-skipping. */
    wasPlaying = false;

    UISetNowPlaying(songs[i].title, songs[i].id);
}

/* Global per-frame tick — must be called from UIUpdate *before* any modal
   early-returns so it still fires while Search / Home / Rename is open. */
void LibraryManagerTick(void)
{
    if (playingLibrary[0] == '\0') return;

    bool currentlyPlaying = YTDLP_IsPlaying();

    if (currentlyPlaying) {
        wasPlaying = true;
        return;
    }

    if (!wasPlaying) return;              /* never actually started        */
    if (YTDLP_IsPaused()) return;          /* paused is not "ended"        */

    /* The song ended naturally. */
    wasPlaying = false;

    if (!repeatAll) {
        /* No auto-advance — drop the context so nothing fires again. */
        playingLibrary[0] = '\0';
        playingSongId[0]  = '\0';
        playingIndex      = -1;
        return;
    }

    /* Reload the *source* library, which may not be the one open now. */
    LibrarySong playlist[MAX_LIBRARY_SONGS];
    int count = LoadLibraryFile(playingLibrary, playlist, MAX_LIBRARY_SONGS);
    if (count == 0) {
        LibraryManagerClearPlaybackSource();
        return;
    }

    /* Prefer matching by id (library may have been edited since). */
    int cur = -1;
    if (playingSongId[0]) {
        for (int i = 0; i < count; ++i) {
            if (strcmp(playlist[i].id, playingSongId) == 0) { cur = i; break; }
        }
    }
    if (cur < 0) cur = playingIndex;
    if (cur < 0 || cur >= count) cur = 0;

    int next = (cur + 1) % count;

    playingIndex = next;
    snprintf(playingSongId, sizeof(playingSongId), "%s", playlist[next].id);
    wasPlaying = false;

    UISetNowPlaying(playlist[next].title, playlist[next].id);

    /* If the library the user is looking at *is* the source library,
       refresh its view so the "Playing" marker and selection stay in sync. */
    if (isOpen && strcmp(currentLibrary, playingLibrary) == 0) {
        songCount = LoadLibraryFile(currentLibrary, songs, MAX_LIBRARY_SONGS);

        int newIdx = -1;
        for (int i = 0; i < songCount; ++i) {
            if (strcmp(songs[i].id, playingSongId) == 0) { newIdx = i; break; }
        }
        if (newIdx >= 0) {
            playingIndex = newIdx;
            selectedSong = newIdx;

            float rowHeight  = 60.0f;
            float viewHeight = (float)GetScreenHeight() - 160.0f;
            float selY       = selectedSong * rowHeight;

            if (selY < scrollOffset)
                scrollOffset = selY;
            else if (selY + rowHeight > scrollOffset + viewHeight)
                scrollOffset = selY + rowHeight - viewHeight;

            if (scrollOffset < 0.0f) scrollOffset = 0.0f;
        }
    }
}

/* =========================================================
   Repeat-All button geometry (shared by Update and Draw)
   ========================================================= */

static Rectangle RepeatButtonRect(int screenWidth)
{
    const char *label = "Repeat All: OFF";   /* widest form */
    Vector2 sz = MeasureTextEx(UIGetFontSized(13), label, 13, 0);
    return (Rectangle){
        (float)screenWidth - sz.x - 56,
        60.0f,
        sz.x + 16.0f,
        28.0f
    };
}

/* =========================================================
   Update
   ========================================================= */

void LibraryManagerUpdate(void)
{
    if (!isOpen) return;

    if (IsKeyPressed(KEY_ESCAPE)) {
        LibraryManagerClose();
        return;
    }

    if (IsKeyPressed(KEY_R))
        LibraryManagerSetRepeatAll(!repeatAll);

    /* Repeat-All button */
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
        CheckCollisionPointRec(GetMousePosition(),
                               RepeatButtonRect(GetScreenWidth()))) {
        LibraryManagerSetRepeatAll(!repeatAll);
    }

    float rowHeight  = 60.0f;
    float viewHeight = (float)GetScreenHeight() - 160.0f;
    float maxScroll  = songCount * rowHeight - viewHeight;
    if (maxScroll < 0.0f) maxScroll = 0.0f;

    /* Wheel scroll */
    if (CheckCollisionPointRec(GetMousePosition(),
            (Rectangle){ 40, 110, (float)GetScreenWidth() - 80, viewHeight })) {
        scrollOffset -= GetMouseWheelMove() * 40.0f;
    }

    /* Click rows */
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        Vector2 mouse = GetMousePosition();
        if (mouse.y >= 110 && mouse.y < 110 + viewHeight) {
            for (int i = 0; i < songCount; ++i) {
                float y = 110 + i * rowHeight - scrollOffset;
                Rectangle row = { 40, y, (float)GetScreenWidth() - 80, 52.0f };
                if (CheckCollisionPointRec(mouse, row)) {
                    if (selectedSong == i)
                        PlaySongAt(i);
                    else
                        selectedSong = i;
                    break;
                }
            }
        }
    }

    if (songCount > 0) {
        if (IsKeyPressed(KEY_DOWN))
            selectedSong = (selectedSong + 1) % songCount;
        if (IsKeyPressed(KEY_UP))
            selectedSong = (selectedSong + songCount - 1) % songCount;

        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER))
            PlaySongAt(selectedSong);

        if (IsKeyPressed(KEY_D) || IsKeyPressed(KEY_DELETE)) {
            int del = selectedSong;

            for (int i = del; i < songCount - 1; ++i)
                songs[i] = songs[i + 1];
            --songCount;

            if (selectedSong >= songCount && songCount > 0)
                selectedSong = songCount - 1;

            SaveLibrarySongs();

            /* Adjust playback context for the deletion. */
            if (playingLibrary[0] &&
                strcmp(playingLibrary, currentLibrary) == 0 &&
                playingIndex >= 0) {
                if (del == playingIndex)
                    LibraryManagerClearPlaybackSource();
                else if (del < playingIndex)
                    --playingIndex;
            }
        }
    }

    /* Keep selection on screen */
    if (songCount > 0) {
        float selectedY = selectedSong * rowHeight;
        if (selectedY < scrollOffset)
            scrollOffset = selectedY;
        else if (selectedY + rowHeight > scrollOffset + viewHeight)
            scrollOffset = selectedY + rowHeight - viewHeight;
    }

    if (scrollOffset < 0.0f)    scrollOffset = 0.0f;
    if (scrollOffset > maxScroll) scrollOffset = maxScroll;
}

/* =========================================================
   Draw
   ========================================================= */

void LibraryManagerDraw(int screenWidth, int screenHeight)
{
    if (!isOpen) return;

    DrawRectangle(0, 0, screenWidth, screenHeight, (Color){ 18, 20, 26, 245 });

    char headerText[128];
    snprintf(headerText, sizeof(headerText), "Library: %s", currentLibrary);
    DrawTextEx(UIGetFontSized(24), headerText, (Vector2){ 40, 40 }, 24, 0,
               (Color){ 239, 241, 247, 255 });

    const char *infoText =
        "Up/Down: Navigate | Enter/Click: Play | D: Delete Song | R: Toggle Repeat | Esc: Back";
    DrawTextEx(UIGetFontSized(13), infoText, (Vector2){ 40, 75 }, 13, 0,
               (Color){ 151, 158, 173, 255 });

    /* Repeat-All button (clickable) */
    {
        Rectangle r = RepeatButtonRect(screenWidth);
        bool hover = CheckCollisionPointRec(GetMousePosition(), r);

        DrawRectangleRounded(r, 0.3f, 6,
            repeatAll ? (Color){ 46, 42, 67, 255 }
                      : (hover ? (Color){ 38, 41, 51, 255 }
                               : (Color){ 26, 29, 37, 255 }));

        char label[32];
        snprintf(label, sizeof(label), "Repeat All: %s",
                 repeatAll ? "ON" : "OFF");
        DrawTextEx(UIGetFontSized(13), label,
                   (Vector2){ r.x + 8, r.y + 7 }, 13, 0,
                   repeatAll ? (Color){ 161, 143, 255, 255 }
                             : (Color){ 151, 158, 173, 255 });
    }

    Rectangle view = { 40, 110,
                       (float)screenWidth - 80,
                       (float)screenHeight - 160 };
    BeginScissorMode((int)view.x, (int)view.y,
                     (int)view.width, (int)view.height);

    if (songCount == 0) {
        DrawTextEx(UIGetFontSized(16), "No songs in this library.",
                   (Vector2){ 40, 130 }, 16, 0,
                   (Color){ 151, 158, 173, 255 });
    } else {
        bool playingHere = (playingLibrary[0] &&
                            strcmp(playingLibrary, currentLibrary) == 0);

        for (int i = 0; i < songCount; ++i) {
            float y = view.y + i * 60.0f - scrollOffset;
            if (y + 52.0f < view.y || y > view.y + view.height) continue;

            Rectangle row = { view.x, y, view.width, 52.0f };
            bool selected = (i == selectedSong);

            DrawRectangleRounded(row, 0.15f, 6,
                selected ? (Color){ 46, 42, 67, 255 }
                         : (Color){ 26, 29, 37, 255 });

            DrawTextEx(UIGetFontSized(15), songs[i].title,
                       (Vector2){ row.x + 16, row.y + 16 }, 15, 0,
                       (Color){ 239, 241, 247, 255 });

            DrawTextEx(UIGetFontSized(13), songs[i].duration,
                       (Vector2){ row.x + row.width - 70, row.y + 18 }, 13, 0,
                       (Color){ 151, 158, 173, 255 });

            if (playingHere && i == playingIndex) {
                DrawTextEx(UIGetFontSized(13), "Playing",
                           (Vector2){ row.x + row.width - 150, row.y + 18 },
                           13, 0, (Color){ 161, 143, 255, 255 });
            }
        }
    }

    EndScissorMode();
}
