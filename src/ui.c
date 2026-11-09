#include "ui.h"

#include "backend/ytdlp.h"
#include "components/library_dialog.h"
#include "components/library_manager.h"
#include "components/search_box.h"

#include "raylib.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    PAGE_HOME,
    PAGE_SEARCH,
    PAGE_LIBRARY
} Page;

static const char *messages[] = {
    "Enjoy music.", "Uncluttered world.", "Find your rhythm.",
    "Music, simply.", "Make space for sound.", "A little more melody.",
    "Let the music in.", "Tune into yourself.", "Sound without noise.",
    "Your quiet corner.", "Press play on calm.", "Good things sound better.",
    "A home for your music.", "Listen a little closer.", "Keep it in tune.",
    "Your day, your sound.", "Music for the moment.", "Less clutter, more music.",
    "Stay in the groove.", "Make room for music.", "Follow the feeling.",
    "Find your frequency.", "Take a musical break.", "Easy listening.",
    "Let sound find you.", "A world in every song.", "Just add music.",
    "Made for listening.", "Soundtrack your day.", "Music feels like home."
};

#define MESSAGE_COUNT ((int)(sizeof(messages) / sizeof(messages[0])))
#define MAX_LIBRARIES 32
#define LIBRARY_NAME_SIZE 64

static const Color BG      = { 18, 20, 26, 255 };
static const Color PANEL   = { 26, 29, 37, 255 };
static const Color UI_WHITE= { 239, 241, 247, 255 };
static const Color MUTED   = { 151, 158, 173, 255 };
static const Color ACCENT  = { 161, 143, 255, 255 };
static const Color BORDER  = { 47, 51, 62, 255 };

static Page  page = PAGE_HOME;
static bool  menuOpen;
static float menuProgress;
static int   messageIndex;
static float resultScroll;
static bool  draggingScrollbar;
static float scrollThumbGrabOffset;
static int   selectedResult;

static char libraries[MAX_LIBRARIES][LIBRARY_NAME_SIZE];
static int  libraryCount;
static int  selectedLibrary;

static char nowPlayingTitle[256];
static bool hasNowPlaying;

/* Grace period so a one-frame IsPlaying()==false during a track switch
   doesn't blank the player bar before LibraryManagerTick has advanced. */
static double lastPlayingTime = 0.0;

/* ---------------- Rename state ---------------- */
static bool renameActive;
static int  renameIndex = -1;
static char renameText[LIBRARY_NAME_SIZE];
static int  renameLen;

/* ---------------- Add Song state ---------------- */
static bool       addSongActive;
static SongResult addSongTarget;
static int        addSongSelectedLib = 0;
static float      addSongScroll      = 0.0f;

/* ---------------- Fonts ---------------- */
#define FONT_SLOTS 10
static const int fontSizes[FONT_SLOTS] = { 12, 13, 14, 15, 16, 17, 19, 20, 26, 32 };
static Font fonts[FONT_SLOTS];
static bool fontsLoaded;

static const char *fontCandidates[] = {
    "fonts/ui.ttf",
    "/usr/share/fonts/TTF/JetBrainsMono-Regular.ttf",
    "/usr/share/fonts/truetype/jetbrains-mono/JetBrainsMono-Regular.ttf",
    "/usr/share/fonts/TTF/Inter-Regular.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/TTF/DejaVuSans.ttf",
    "/usr/share/fonts/noto/NotoSans-Regular.ttf",
    "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf"
};

static int *BuildCodepoints(int *count)
{
    static const int ranges[][2] = {
        { 32, 126 }, { 160, 591 }, { 0x370, 0x3FF }, { 0x400, 0x4FF },
        { 0x2010, 0x2027 }, { 0x2030, 0x205E }, { 0x20A0, 0x20BF }
    };
    int rangeCount = (int)(sizeof(ranges) / sizeof(ranges[0]));
    int total = 0;

    for (int r = 0; r < rangeCount; r++)
        total += ranges[r][1] - ranges[r][0] + 1;

    int *codepoints = malloc((size_t)total * sizeof(int));
    if (!codepoints) { *count = 0; return NULL; }

    int n = 0;
    for (int r = 0; r < rangeCount; r++)
        for (int c = ranges[r][0]; c <= ranges[r][1]; c++)
            codepoints[n++] = c;

    *count = n;
    return codepoints;
}

static void LoadFonts(void)
{
    const char *path = NULL;
    for (int i = 0; i < (int)(sizeof(fontCandidates) / sizeof(fontCandidates[0])); i++) {
        if (FileExists(fontCandidates[i])) { path = fontCandidates[i]; break; }
    }

    Font fallback = GetFontDefault();
    for (int i = 0; i < FONT_SLOTS; i++) fonts[i] = fallback;
    if (!path) return;

    int cpCount = 0;
    int *codepoints = BuildCodepoints(&cpCount);

    bool ok = true;
    Font loaded[FONT_SLOTS];
    for (int i = 0; i < FONT_SLOTS; i++) {
        loaded[i] = LoadFontEx(path, fontSizes[i], codepoints, cpCount);
        if (loaded[i].texture.id == 0) {
            for (int j = 0; j < i; j++) UnloadFont(loaded[j]);
            ok = false;
            break;
        }
        SetTextureFilter(loaded[i].texture, TEXTURE_FILTER_BILINEAR);
    }
    free(codepoints);

    if (ok) {
        for (int i = 0; i < FONT_SLOTS; i++) fonts[i] = loaded[i];
        fontsLoaded = true;
    }
}

static Font FontForSize(float size)
{
    int best = 0;
    float bestDiff = 1e9f;
    for (int i = 0; i < FONT_SLOTS; i++) {
        float diff = fabsf((float)fontSizes[i] - size);
        if (diff < bestDiff) { bestDiff = diff; best = i; }
    }
    return fonts[best];
}

Font UIGetFont(void)             { return FontForSize(20.0f); }
Font UIGetFontSized(float size)  { return FontForSize(size); }

static void Text(const char *text, Vector2 pos, float size, Color color)
{
    pos.x = roundf(pos.x);
    pos.y = roundf(pos.y);
    DrawTextEx(FontForSize(size), text, pos, size, 0.0f, color);
}

static float TextWidth(const char *text, float size)
{
    return MeasureTextEx(FontForSize(size), text, size, 0.0f).x;
}

static void FitText(const char *src, float size, float maxWidth,
                    char *out, size_t outSize)
{
    snprintf(out, outSize, "%s", src);
    if (TextWidth(out, size) <= maxWidth) return;

    size_t len = strlen(out);
    while (len > 0) {
        len--;
        while (len > 0 && ((unsigned char)out[len] & 0xC0) == 0x80) len--;
        snprintf(out + len, outSize - len, "...");
        if (TextWidth(out, size) <= maxWidth) return;
    }
}

/* ---------------- Layout helpers ---------------- */
static Rectangle ResultsViewRect(int screenHeight)
{
    float height = screenHeight - 338.0f;
    if (height < 80.0f) height = 80.0f;
    return (Rectangle){ 40.0f, 250.0f,
                        (float)GetScreenWidth() - 80.0f, height };
}
static Rectangle SearchButtonRect(int screenWidth)
{
    return (Rectangle){ screenWidth - 144.0f, 158.0f, 104.0f, 52.0f };
}
static Rectangle NormalizeRect(int screenWidth)
{
    return (Rectangle){ screenWidth - 170.0f, 20.0f, 130.0f, 32.0f };
}
static bool CtrlHeld(void)
{
    return IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
}

static void HandlePageShortcuts(void)
{
    if (!CtrlHeld()) return;

    bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    bool changed = true;

    if (IsKeyPressed(KEY_ONE) || IsKeyPressed(KEY_KP_1))        page = PAGE_HOME;
    else if (IsKeyPressed(KEY_TWO) || IsKeyPressed(KEY_KP_2))   page = PAGE_SEARCH;
    else if (IsKeyPressed(KEY_THREE) || IsKeyPressed(KEY_KP_3)) page = PAGE_LIBRARY;
    else if (IsKeyPressed(KEY_FOUR) || IsKeyPressed(KEY_KP_4))  LibraryDialogOpen();
    else if (IsKeyPressed(KEY_TAB)) page = (Page)((page + (shift ? 2 : 1)) % 3);
    else changed = false;

    if (changed) { menuOpen = false; draggingScrollbar = false; }
}

/* ------------------------------------------------------------------ */
/* Search entry point (Enter in box, or the Search button)             */
/*                                                                     */
/*   - non-empty query → start a real search.                          */
/*   - empty query     → clear cached results so the page falls back   */
/*                       to its default "Enter a search..." state.     */
/* ------------------------------------------------------------------ */
static void StartSearch(void)
{
    const char *query = SearchBoxText();

    if (query && query[0]) {
        if (!YTDLP_IsSearching()) {
            resultScroll = 0.0f;
            selectedResult = 0;
            YTDLP_SearchAsync(query);
        }
    } else {
        /* Empty box + Enter (or Search button): reset to default page. */
        YTDLP_ClearResults();
        resultScroll = 0.0f;
        selectedResult = 0;
        draggingScrollbar = false;
    }

    SearchBoxClearSubmitted();
}

/* Public: used by the Library manager.  Does NOT clear the playback source
   because the manager sets it before calling us. */
void UISetNowPlaying(const char *title, const char *id)
{
    if (!id || !id[0]) return;
    snprintf(nowPlayingTitle, sizeof(nowPlayingTitle),
             "%s", title ? title : "Unknown Title");
    YTDLP_PlaySong(id);
    hasNowPlaying = true;
    lastPlayingTime = GetTime();
}

/* Search-result playback: LibraryManager is no longer the source. */
static void SetNowPlaying(const SongResult *result)
{
    if (!result || !result->id[0]) return;
    LibraryManagerClearPlaybackSource();
    UISetNowPlaying(result->title, result->id);
}

static void DrawPlayIcon(float x, float y, float size, Color color)
{
    DrawTriangle((Vector2){ x, y },
                 (Vector2){ x, y + size },
                 (Vector2){ x + size * 0.82f, y + size * 0.5f },
                 color);
}

static float ResultContentHeight(int count)
{
    if (count <= 0) return 0.0f;
    return count * 80.0f - 8.0f;
}
static float ResultMaxScroll(int count, float viewHeight)
{
    float m = ResultContentHeight(count) - viewHeight;
    return m > 0.0f ? m : 0.0f;
}
static Rectangle ScrollTrack(Rectangle view, int screenWidth)
{
    return (Rectangle){ screenWidth - 31.0f, view.y, 5.0f, view.height };
}
static Rectangle ScrollThumb(Rectangle view, int count, int screenWidth)
{
    float contentHeight = ResultContentHeight(count);
    float thumbHeight = view.height;
    if (contentHeight > view.height && contentHeight > 0.0f) {
        thumbHeight = view.height * view.height / contentHeight;
        if (thumbHeight < 28.0f) thumbHeight = 28.0f;
    }
    float maxScroll = ResultMaxScroll(count, view.height);
    float thumbTravel = view.height - thumbHeight;
    float thumbY = view.y;
    if (maxScroll > 0.0f && thumbTravel > 0.0f)
        thumbY += (resultScroll / maxScroll) * thumbTravel;
    return (Rectangle){ screenWidth - 34.0f, thumbY, 11.0f, thumbHeight };
}

/* ---------------- Library list ---------------- */
static void GetLibraryPath(const char *libName, char *out, size_t outSize)
{
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && xdg[0]) {
        snprintf(out, outSize, "%s/ea-music/%s.lib", xdg, libName);
        return;
    }
    const char *home = getenv("HOME");
    if (home && home[0]) {
        snprintf(out, outSize, "%s/.config/ea-music/%s.lib", home, libName);
        return;
    }
    snprintf(out, outSize, "%s.lib", libName);
}

static void SaveSongToLibrary(const char *libName, const SongResult *song)
{
    char filepath[512];
    GetLibraryPath(libName, filepath, sizeof(filepath));
    FILE *f = fopen(filepath, "a");
    if (f) {
        fprintf(f, "%s|%s|%s\n", song->title, song->id,
                song->duration[0] ? song->duration : "--:--");
        fclose(f);
    }
}

static void DeleteLibrary(int index)
{
    if (index < 0 || index >= libraryCount) return;

    char filepath[512];
    GetLibraryPath(libraries[index], filepath, sizeof(filepath));
    remove(filepath);

    for (int j = index; j < libraryCount - 1; j++)
        snprintf(libraries[j], LIBRARY_NAME_SIZE, "%s", libraries[j + 1]);
    libraryCount--;

    if (selectedLibrary > index) selectedLibrary--;
    if (libraryCount == 0) selectedLibrary = 0;
    else if (selectedLibrary >= libraryCount) selectedLibrary = libraryCount - 1;
}

/* Mirrors library_dialog.c's filename rules so names entered through
   the rename dialog behave the same as names entered through the
   "New library" dialog.  Blocks path separators and the other chars
   Win32 / shells treat specially. */
static bool IsAllowedLibraryChar(int ch)
{
    if (ch < 32 || ch >= 127) return false;
    switch (ch) {
        case '/': case '\\': case ':': case '*': case '?':
        case '"': case '<': case '>': case '|':
            return false;
    }
    return true;
}

/* Full-name check for CommitRename: per-char rules plus a leading-dot
   guard so ".", ".." and ".hidden" can't be created and thus can't
   escape the config directory via "..". */
static bool IsAllowedLibraryName(const char *name)
{
    if (!name || !name[0]) return false;
    if (name[0] == '.') return false;
    for (const char *q = name; *q; q++)
        if (!IsAllowedLibraryChar((unsigned char)*q)) return false;
    return true;
}

static void OpenRename(int index)
{
    if (index < 0 || index >= libraryCount) return;
    renameActive = true;
    renameIndex = index;
    snprintf(renameText, sizeof(renameText), "%s", libraries[index]);
    renameLen = (int)strlen(renameText);
}

static void CommitRename(void)
{
    if (!renameActive || renameIndex < 0 || renameIndex >= libraryCount) {
        renameActive = false;
        return;
    }
    if (renameLen == 0 || strcmp(renameText, libraries[renameIndex]) == 0) {
        renameActive = false;
        return;
    }
    /* Path-traversal / dotfile guard: without this a name like
       "../evil" or ".hidden" would land outside (or hidden inside)
       the config directory.  RenameUpdate already blocks these
       chars during typing; this is the belt-and-braces check at
       commit time so a paste or a legacy bad name can't slip in. */
    if (!IsAllowedLibraryName(renameText)) {
        renameActive = false;
        return;
    }
    for (int i = 0; i < libraryCount; i++) {
        if (i != renameIndex && strcmp(libraries[i], renameText) == 0) {
            renameActive = false;
            return;
        }
    }
    char oldPath[512], newPath[512];
    GetLibraryPath(libraries[renameIndex], oldPath, sizeof(oldPath));
    GetLibraryPath(renameText, newPath, sizeof(newPath));
    if (rename(oldPath, newPath) == 0)
        snprintf(libraries[renameIndex], LIBRARY_NAME_SIZE, "%s", renameText);
    renameActive = false;
}

static void RenameUpdate(void)
{
    int ch;
    while ((ch = GetCharPressed()) > 0) {
        /* Reject hostile chars as they're typed so the user gets
           immediate feedback instead of a silent no-op on Enter. */
        if (ch == '.' && renameLen == 0) continue;  /* no ".", "..", dotfiles */
        if (IsAllowedLibraryChar(ch) && renameLen < LIBRARY_NAME_SIZE - 1) {
            renameText[renameLen++] = (char)ch;
            renameText[renameLen] = '\0';
        }
    }
    if ((IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) &&
        renameLen > 0) {
        renameLen--;
        renameText[renameLen] = '\0';
    }
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) CommitRename();
    else if (IsKeyPressed(KEY_ESCAPE)) renameActive = false;
}

static void RenameDraw(int screenWidth, int screenHeight)
{
    if (!renameActive) return;
    DrawRectangle(0, 0, screenWidth, screenHeight, (Color){ 0, 0, 0, 170 });

    int modalW = 440, modalH = 180;
    int mx = (screenWidth - modalW) / 2;
    int my = (screenHeight - modalH) / 2;
    Rectangle modal = { (float)mx, (float)my, (float)modalW, (float)modalH };

    DrawRectangleRounded(modal, 0.08f, 8, PANEL);
    DrawRectangleRoundedLines(modal, 0.08f, 8, BORDER);
    Text("Rename library", (Vector2){ (float)mx + 24, (float)my + 20 }, 20, UI_WHITE);

    Rectangle input = { (float)mx + 24, (float)my + 60,
                        (float)modalW - 48, 44 };
    DrawRectangleRounded(input, 0.15f, 8, (Color){ 38, 41, 51, 255 });
    Text(renameText, (Vector2){ input.x + 12, input.y + 12 }, 16, UI_WHITE);

    if (((int)(GetTime() * 2)) % 2 == 0) {
        float cx = input.x + 12 + TextWidth(renameText, 16);
        DrawRectangle((int)cx, (int)input.y + 10, 2, 24, ACCENT);
    }
    Text("Enter to confirm  ·  Esc to cancel",
         (Vector2){ (float)mx + 24, (float)my + modalH - 32 }, 12, MUTED);
}

/* ---------------- Add Song modal ---------------- */
#define ADD_SONG_MODAL_W 460
#define ADD_SONG_ROW_H   46.0f

static Rectangle AddSongModalRect(int screenWidth, int screenHeight)
{
    int modalH = 260 + libraryCount * (int)ADD_SONG_ROW_H;
    if (modalH > 500) modalH = 500;
    int mx = (screenWidth  - ADD_SONG_MODAL_W) / 2;
    int my = (screenHeight - modalH) / 2;
    return (Rectangle){ (float)mx, (float)my,
                        (float)ADD_SONG_MODAL_W, (float)modalH };
}

/* The scrollable list area: everything between the subtitle (my + 50)
   and the footer hint (my + modalH - 32). */
static Rectangle AddSongListView(Rectangle modal)
{
    return (Rectangle){ modal.x + 24, modal.y + 80,
                        modal.width - 48, modal.height - 80 - 32 };
}

static float AddSongMaxScroll(Rectangle listView)
{
    float content = libraryCount * ADD_SONG_ROW_H;
    float m = content - listView.height;
    return m > 0.0f ? m : 0.0f;
}

static void AddSongEnsureVisible(Rectangle listView)
{
    float rowTop    = addSongSelectedLib * ADD_SONG_ROW_H;
    float rowBottom = rowTop + ADD_SONG_ROW_H;
    if (rowTop < addSongScroll)
        addSongScroll = rowTop;
    else if (rowBottom > addSongScroll + listView.height)
        addSongScroll = rowBottom - listView.height;
}

static void OpenAddSong(const SongResult *song)
{
    if (!song || !song->id[0]) return;
    addSongTarget = *song;
    addSongSelectedLib = 0;
    addSongScroll = 0.0f;
    addSongActive = true;
}

static void AddSongUpdate(void)
{
    if (!addSongActive) return;
    if (IsKeyPressed(KEY_ESCAPE)) { addSongActive = false; return; }

    Rectangle modal    = AddSongModalRect(GetScreenWidth(), GetScreenHeight());
    Rectangle listView = AddSongListView(modal);

    /* Wheel scroll only when content overflows and the cursor is over
       the list, so the outer search page stays unaware of the scroll. */
    if (libraryCount * ADD_SONG_ROW_H > listView.height &&
        CheckCollisionPointRec(GetMousePosition(), listView)) {
        addSongScroll -= GetMouseWheelMove() * ADD_SONG_ROW_H;
    }

    if (libraryCount > 0) {
        if (IsKeyPressed(KEY_DOWN) || IsKeyPressedRepeat(KEY_DOWN))
            addSongSelectedLib = (addSongSelectedLib + 1) % libraryCount;
        if (IsKeyPressed(KEY_UP) || IsKeyPressedRepeat(KEY_UP))
            addSongSelectedLib = (addSongSelectedLib + libraryCount - 1) % libraryCount;
        AddSongEnsureVisible(listView);

        if (IsKeyPressed(KEY_ENTER) || IsKeyPressedRepeat(KEY_ENTER)) {
            SaveSongToLibrary(libraries[addSongSelectedLib], &addSongTarget);
            addSongActive = false;
            return;
        }
    }

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        Vector2 mouse = GetMousePosition();
        /* Click hit-test is scoped to the visible list rect so rows
           clipped by the scissor can never be clicked. */
        if (CheckCollisionPointRec(mouse, listView)) {
            for (int i = 0; i < libraryCount; i++) {
                Rectangle r = { listView.x,
                                listView.y + i * ADD_SONG_ROW_H - addSongScroll,
                                listView.width, 38 };
                if (CheckCollisionPointRec(mouse, r)) {
                    addSongSelectedLib = i;
                    SaveSongToLibrary(libraries[i], &addSongTarget);
                    addSongActive = false;
                    return;
                }
            }
        }
    }

    float maxScroll = AddSongMaxScroll(listView);
    if (addSongScroll < 0.0f)      addSongScroll = 0.0f;
    if (addSongScroll > maxScroll) addSongScroll = maxScroll;
}

static void AddSongDraw(int screenWidth, int screenHeight)
{
    if (!addSongActive) return;
    DrawRectangle(0, 0, screenWidth, screenHeight, (Color){ 0, 0, 0, 170 });

    Rectangle modal    = AddSongModalRect(screenWidth, screenHeight);
    Rectangle listView = AddSongListView(modal);
    int mx = (int)modal.x;
    int my = (int)modal.y;

    DrawRectangleRounded(modal, 0.08f, 8, PANEL);
    DrawRectangleRoundedLines(modal, 0.08f, 8, BORDER);
    Text("Add Song to Library", (Vector2){ (float)mx + 24, (float)my + 20 }, 20, UI_WHITE);
    Text("Choose a library below (Up/Down + Enter or Click):",
         (Vector2){ (float)mx + 24, (float)my + 50 }, 13, MUTED);

    if (libraryCount == 0) {
        Text("No libraries found. Create one first!",
             (Vector2){ (float)mx + 24, (float)my + 100 }, 14,
             (Color){ 255, 150, 150, 255 });
    } else {
        BeginScissorMode((int)listView.x, (int)listView.y,
                         (int)listView.width, (int)listView.height);
        for (int i = 0; i < libraryCount; i++) {
            float y = listView.y + i * ADD_SONG_ROW_H - addSongScroll;
            if (y + 38.0f < listView.y || y > listView.y + listView.height)
                continue;
            Rectangle r = { listView.x, y, listView.width, 38 };
            bool sel = (i == addSongSelectedLib);
            DrawRectangleRounded(r, 0.15f, 6,
                sel ? (Color){ 46, 42, 67, 255 } : (Color){ 38, 41, 51, 255 });
            Text(libraries[i], (Vector2){ r.x + 16, r.y + 10 }, 15,
                 sel ? ACCENT : UI_WHITE);
        }
        EndScissorMode();

        /* Scrollbar hint, only when the list overflows. */
        float maxScroll = AddSongMaxScroll(listView);
        if (maxScroll > 0.0f) {
            Rectangle track = { listView.x + listView.width - 4,
                                listView.y, 3, listView.height };
            DrawRectangleRounded(track, 0.5f, 4, (Color){ 37, 40, 49, 255 });

            float thumbH = listView.height * listView.height /
                           (libraryCount * ADD_SONG_ROW_H);
            if (thumbH < 24.0f) thumbH = 24.0f;
            float thumbY = listView.y +
                (addSongScroll / maxScroll) * (listView.height - thumbH);
            Rectangle thumb = { track.x - 2, thumbY, 7, thumbH };
            DrawRectangleRounded(thumb, 0.5f, 4, ACCENT);
        }
    }
    Text("Esc to cancel", (Vector2){ (float)mx + 24, (float)my + modal.height - 32 }, 12, MUTED);
}

/* ---------------- Normalize toggle ---------------- */
static void DrawNormalizeToggle(int screenWidth)
{
    Rectangle r = NormalizeRect(screenWidth);
    bool on = YTDLP_GetNormalize();
    bool hover = CheckCollisionPointRec(GetMousePosition(), r);

    DrawRectangleRounded(r, 0.4f, 8,
                         hover ? (Color){ 38, 41, 51, 255 } : PANEL);
    Text("Normalize", (Vector2){ r.x + 12, r.y + 9 }, 14,
         on ? UI_WHITE : MUTED);

    Rectangle track = { r.x + r.width - 46, r.y + 8, 34, 16 };
    DrawRectangleRounded(track, 1.0f, 10,
                         on ? ACCENT : (Color){ 63, 66, 78, 255 });
    float knobX = on ? track.x + track.width - 8 : track.x + 8;
    DrawCircle((int)knobX, (int)(track.y + 8), 6,
               on ? (Color){ 24, 22, 31, 255 } : UI_WHITE);
}

/* ---------------- Search results ---------------- */
static void DrawSearchResults(int screenWidth, int screenHeight)
{
    SongResult results[MAX_RESULTS];
    int count = YTDLP_GetResults(results, MAX_RESULTS);
    Rectangle view = ResultsViewRect(screenHeight);

    if (YTDLP_IsSearching()) {
        Text("Searching YouTube Music...", (Vector2){ 40, 226 }, 14, MUTED);
    } else if (count == 0) {
        Text("Enter a search and press Enter or Search.",
             (Vector2){ 40, 226 }, 14, MUTED);
    } else {
        Text("Search results  (Shift+Enter plays  ·  Ctrl+A adds to library)",
             (Vector2){ 40, 226 }, 14, MUTED);
    }

    if (count <= 0) return;

    if (selectedResult >= count) selectedResult = count - 1;
    if (selectedResult < 0) selectedResult = 0;

    float maxScroll = ResultMaxScroll(count, view.height);
    if (resultScroll > maxScroll) resultScroll = maxScroll;
    if (resultScroll < 0.0f) resultScroll = 0.0f;

    BeginScissorMode((int)view.x, (int)view.y,
                     (int)view.width, (int)view.height);

    for (int i = 0; i < count; i++) {
        float y = view.y + i * 80.0f - resultScroll;
        Rectangle row = { view.x, y, view.width, 72.0f };

        DrawRectangleRounded(row, 0.14f, 8,
            i == selectedResult
                ? (Color){ 39, 38, 53, 255 }
                : PANEL);

        // Thumbnail design box placeholder (retains exact visual layout)
        DrawRectangleRounded(
            (Rectangle){ row.x + 8, row.y + 6, 96, 60 },
            0.12f, 6, (Color){ 38, 41, 51, 255 });
        DrawPlayIcon(row.x + 46, row.y + 24, 22,
                     i == selectedResult ? ACCENT : MUTED);

        float titleMaxWidth = row.width - 230.0f;
        char fittedTitle[256];
        FitText(results[i].title, 14, titleMaxWidth, fittedTitle, sizeof(fittedTitle));

        Text(fittedTitle,
             (Vector2){ row.x + 116, row.y + 14 },
             14, UI_WHITE);

        Text(results[i].duration[0] ? results[i].duration : "--:--",
             (Vector2){ row.x + 116, row.y + 43 },
             13, MUTED);

        Rectangle addBtn = { row.x + row.width - 120, row.y + 20, 70, 32 };
        DrawRectangleRounded(addBtn, 0.2f, 6, (Color){ 52, 44, 63, 255 });
        Text("Add", (Vector2){ addBtn.x + 22, addBtn.y + 8 }, 13, ACCENT);

        DrawPlayIcon(row.x + row.width - 35, row.y + 27, 18, ACCENT);
    }

    EndScissorMode();

    if (maxScroll > 0.0f) {
        Rectangle track = ScrollTrack(view, screenWidth);
        Rectangle thumb = ScrollThumb(view, count, screenWidth);
        DrawRectangleRounded(track, 0.5f, 6,
                           (Color){ 37, 40, 49, 255 });
        DrawRectangleRounded(thumb, 0.5f, 6, ACCENT);
    }
}

/* ---------------- Player bar ---------------- */
typedef struct {
    Rectangle bar, pause, timeline, loop, volumeIcon, volumeSlider, stop;
} PlayerLayout;

static bool  draggingTimeline;
static bool  draggingVolume;
static float dragFraction;
static int   premuteVolume = YTDLP_VOLUME_DEFAULT;
static double volumeLabelUntil;

static float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

static void ChangeVolume(int percent)
{
    YTDLP_SetVolume(percent);
    volumeLabelUntil = GetTime() + 1.2;
}
static void ToggleMute(void)
{
    if (YTDLP_GetVolume() > 0) {
        premuteVolume = YTDLP_GetVolume();
        ChangeVolume(0);
    } else {
        ChangeVolume(premuteVolume > 0 ? premuteVolume : YTDLP_VOLUME_DEFAULT);
    }
}

static PlayerLayout GetPlayerLayout(int sw, int sh)
{
    PlayerLayout L;
    L.bar = (Rectangle){ 24.0f, sh - 76.0f, sw - 48.0f, 60.0f };
    L.pause = (Rectangle){ L.bar.x + 243, L.bar.y + 10, 40, 40 };
    L.stop  = (Rectangle){ L.bar.x + L.bar.width - 82, L.bar.y + 10, 68, 40 };
    L.volumeSlider = (Rectangle){ L.stop.x - 16 - 100, L.bar.y + 18, 100, 24 };
    L.volumeIcon   = (Rectangle){ L.volumeSlider.x - 8 - 24, L.bar.y + 18, 24, 24 };
    L.loop         = (Rectangle){ L.volumeIcon.x - 12 - 52, L.bar.y + 14, 52, 32 };
    float tx = L.bar.x + 300;
    L.timeline = (Rectangle){ tx, L.bar.y + 18, L.loop.x - 16 - tx, 24 };
    return L;
}

static void DrawSlider(Rectangle hit, float fraction, bool active)
{
    bool hover = active || CheckCollisionPointRec(GetMousePosition(), hit);
    float cy = hit.y + hit.height / 2.0f;
    Rectangle track = { hit.x, cy - 2.5f, hit.width, 5.0f };

    DrawRectangleRounded(track, 0.5f, 6, (Color){ 63, 66, 78, 255 });
    float w = track.width * fraction;
    if (w >= 2.0f) {
        DrawRectangleRounded((Rectangle){ track.x, track.y, w, track.height },
                             0.5f, 6,
                             hover ? (Color){ 181, 166, 255, 255 } : ACCENT);
    }
    if (hover) DrawCircle((int)(track.x + w), (int)cy, 6, UI_WHITE);
}

static void DrawSpeakerIcon(float x, float y, int vol, Color color)
{
    DrawRectangleRounded((Rectangle){ x + 3, y + 9, 5, 6 }, 0.3f, 4, color);
    DrawTriangle((Vector2){ x + 8, y + 9 }, (Vector2){ x + 8, y + 15 },
                 (Vector2){ x + 13, y + 19 }, color);
    DrawTriangle((Vector2){ x + 8, y + 9 }, (Vector2){ x + 13, y + 19 },
                 (Vector2){ x + 13, y + 5 }, color);
    Vector2 c = { x + 13, y + 12 };
    if (vol <= 0) {
        DrawLineEx((Vector2){ x + 16, y + 8 }, (Vector2){ x + 22, y + 16 }, 2, color);
        DrawLineEx((Vector2){ x + 22, y + 8 }, (Vector2){ x + 16, y + 16 }, 2, color);
        return;
    }
    DrawRing(c, 5.0f, 6.5f, -40, 40, 12, color);
    if (vol >= 70) DrawRing(c, 9.0f, 10.5f, -40, 40, 16, color);
}

static void HandleShortcuts(void)
{
    if (page == PAGE_SEARCH && !CtrlHeld()) return;

    if (IsKeyPressed(KEY_SPACE)) YTDLP_TogglePause();
    if (IsKeyPressed(KEY_M)) ToggleMute();
    if (IsKeyPressed(KEY_L)) YTDLP_SetLoop(!YTDLP_GetLoop());
    if (IsKeyPressed(KEY_N)) YTDLP_SetNormalize(!YTDLP_GetNormalize());

    if (IsKeyPressed(KEY_S)) {
        LibraryManagerClearPlaybackSource();
        YTDLP_StopPlayback();
        hasNowPlaying = false;
        draggingTimeline = draggingVolume = false;
    }

    bool onLibraryNav = (page == PAGE_LIBRARY && !CtrlHeld());
    if (!onLibraryNav) {
        if (IsKeyPressed(KEY_UP) || IsKeyPressedRepeat(KEY_UP))
            ChangeVolume(YTDLP_GetVolume() + 5);
        if (IsKeyPressed(KEY_DOWN) || IsKeyPressedRepeat(KEY_DOWN))
            ChangeVolume(YTDLP_GetVolume() - 5);
    }

    bool left  = IsKeyPressed(KEY_LEFT)  || IsKeyPressedRepeat(KEY_LEFT);
    bool right = IsKeyPressed(KEY_RIGHT) || IsKeyPressedRepeat(KEY_RIGHT);
    if (left || right) {
        double t = YTDLP_GetTime();
        if (t >= 0.0) YTDLP_Seek(t + (right ? 5.0 : -5.0));
    }
}

static void UpdatePlayerBar(int sw, int sh)
{
    if (!hasNowPlaying) {
        draggingTimeline = draggingVolume = false;
        return;
    }

    /* Grace period so a one-frame IsPlaying()==false during a track
       switch (LibraryManagerTick advancing) doesn't blank the bar. */
    if (!YTDLP_IsPlaying()) {
        if (GetTime() - lastPlayingTime > 0.5) {
            hasNowPlaying = false;
            draggingTimeline = draggingVolume = false;
        }
        return;
    }
    lastPlayingTime = GetTime();

    HandleShortcuts();
    if (!hasNowPlaying) return;

    PlayerLayout L = GetPlayerLayout(sw, sh);
    Vector2 mouse = GetMousePosition();

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        Vector2 pc = { L.pause.x + 20, L.pause.y + 20 };

        if (CheckCollisionPointRec(mouse, L.stop)) {
            LibraryManagerClearPlaybackSource();
            YTDLP_StopPlayback();
            hasNowPlaying = false;
            draggingTimeline = draggingVolume = false;
            return;
        }

        if (CheckCollisionPointCircle(mouse, pc, 20)) {
            YTDLP_TogglePause();
        } else if (CheckCollisionPointRec(mouse, L.volumeIcon)) {
            ToggleMute();
        } else if (CheckCollisionPointRec(mouse, L.loop)) {
            YTDLP_SetLoop(!YTDLP_GetLoop());
        } else if (CheckCollisionPointRec(mouse, L.volumeSlider)) {
            draggingVolume = true;
        } else if (L.timeline.width > 40.0f &&
                   CheckCollisionPointRec(mouse, L.timeline) &&
                   YTDLP_GetDuration() > 0.0) {
            draggingTimeline = true;
        }
    }

    Rectangle volumeArea = { L.volumeIcon.x, L.volumeIcon.y,
                             L.volumeSlider.x + L.volumeSlider.width - L.volumeIcon.x,
                             L.volumeIcon.height };
    if (CheckCollisionPointRec(mouse, volumeArea)) {
        float wheel = GetMouseWheelMove();
        if (wheel != 0.0f)
            ChangeVolume(YTDLP_GetVolume() + (int)(wheel * 5.0f));
    }

    if (draggingVolume) {
        float r = Clamp01((mouse.x - L.volumeSlider.x) / L.volumeSlider.width);
        ChangeVolume((int)(r * YTDLP_VOLUME_MAX + 0.5f));
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) draggingVolume = false;
    }

    if (draggingTimeline) {
        dragFraction = Clamp01((mouse.x - L.timeline.x) / L.timeline.width);
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            double duration = YTDLP_GetDuration();
            if (duration > 0.0) YTDLP_Seek(dragFraction * duration);
            draggingTimeline = false;
        }
    }
}

static void DrawPlayerBar(int screenWidth, int screenHeight)
{
    if (!hasNowPlaying) return;

    PlayerLayout L = GetPlayerLayout(screenWidth, screenHeight);
    Rectangle bar = L.bar;
    Vector2 mouse = GetMousePosition();
    Color dark = { 24, 22, 31, 255 };

    DrawRectangleRounded(bar, 0.18f, 8, (Color){ 31, 34, 43, 255 });
    DrawRectangleRounded((Rectangle){ bar.x + 8, bar.y + 7, 46, 46 },
                         0.12f, 6, (Color){ 38, 41, 51, 255 });
    DrawPlayIcon(bar.x + 23, bar.y + 19, 19, ACCENT);

    double current  = YTDLP_GetTime();
    double duration = YTDLP_GetDuration();
    bool known = current >= 0.0 && duration > 0.0;

    BeginScissorMode((int)(bar.x + 64), (int)bar.y, 170, (int)bar.height);
    Text(nowPlayingTitle, (Vector2){ bar.x + 64, bar.y + 9 }, 14, UI_WHITE);
    EndScissorMode();

    double shown = (draggingTimeline && duration > 0.0)
                       ? dragFraction * duration : current;

    char timeText[64];
    if (known) {
        snprintf(timeText, sizeof(timeText), "%02d:%02d / %02d:%02d%s%s",
                 (int)shown / 60, (int)shown % 60,
                 (int)duration / 60, (int)duration % 60,
                 YTDLP_GetLoop() ? " - Loop" : "",
                 YTDLP_IsPaused() ? " - Paused" : "");
    } else {
        snprintf(timeText, sizeof(timeText), "Loading playback...");
    }
    Text(timeText, (Vector2){ bar.x + 64, bar.y + 33 }, 12, MUTED);

    Vector2 pc = { L.pause.x + 20, L.pause.y + 20 };
    bool hoverPause = CheckCollisionPointCircle(mouse, pc, 20);
    DrawCircleV(pc, 20, hoverPause ? (Color){ 181, 166, 255, 255 } : ACCENT);
    if (YTDLP_IsPaused()) {
        DrawPlayIcon(pc.x - 5, pc.y - 8, 16, dark);
    } else {
        DrawRectangleRounded((Rectangle){ pc.x - 7, pc.y - 8, 5, 16 }, 0.3f, 4, dark);
        DrawRectangleRounded((Rectangle){ pc.x + 2, pc.y - 8, 5, 16 }, 0.3f, 4, dark);
    }

    if (L.timeline.width > 40.0f) {
        float fraction = 0.0f;
        if (draggingTimeline) fraction = dragFraction;
        else if (known)       fraction = Clamp01((float)(current / duration));
        DrawSlider(L.timeline, fraction, draggingTimeline);
    }

    {
        bool on    = YTDLP_GetLoop();
        bool hover = CheckCollisionPointRec(mouse, L.loop);
        DrawRectangleRounded(L.loop, 0.3f, 8,
            on ? ACCENT : (hover ? (Color){ 52, 56, 68, 255 }
                                 : (Color){ 38, 41, 51, 255 }));
        float w = TextWidth("Loop", 13);
        Text("Loop", (Vector2){ L.loop.x + (L.loop.width - w) / 2.0f, L.loop.y + 9 },
             13, on ? dark : MUTED);
    }

    int vol = YTDLP_GetVolume();
    bool hoverIcon = CheckCollisionPointRec(mouse, L.volumeIcon);
    DrawSpeakerIcon(L.volumeIcon.x, L.volumeIcon.y, vol,
                    hoverIcon ? UI_WHITE : MUTED);
    DrawSlider(L.volumeSlider, (float)vol / YTDLP_VOLUME_MAX, draggingVolume);

    bool showLabel = draggingVolume || GetTime() < volumeLabelUntil ||
                     CheckCollisionPointRec(mouse, L.volumeSlider);
    if (showLabel) {
        char volText[16];
        snprintf(volText, sizeof(volText), "%d%%", vol);
        float w = TextWidth(volText, 12);
        Text(volText,
             (Vector2){ L.volumeSlider.x + (L.volumeSlider.width - w) / 2.0f,
                        bar.y + 3 },
             12, UI_WHITE);
    }

    DrawRectangleRounded(L.stop, 0.2f, 8, (Color){ 52, 44, 63, 255 });
    Text("Stop", (Vector2){ L.stop.x + 18, L.stop.y + 12 }, 14, ACCENT);
}

/* ---------------- Menu / navigation ---------------- */
static void DrawMenuIcon(void)
{
    for (int i = 0; i < 3; i++)
        DrawRectangleRounded((Rectangle){ 28, 27 + i * 7.0f, 19, 2 }, 0.5f, 4, UI_WHITE);
}
static void DrawNavButton(Rectangle rect, const char *label, bool active)
{
    DrawRectangleRounded(rect, 0.2f, 8,
                         active ? (Color){ 46, 42, 67, 255 } : PANEL);
    Text(label, (Vector2){ rect.x + 16, rect.y + 13 }, 17,
         active ? ACCENT : UI_WHITE);
}

void UIInit(void)
{
    SetExitKey(KEY_NULL);
    LoadFonts();
    messageIndex = GetRandomValue(0, MESSAGE_COUNT - 1);
    LibraryManagerInit();
    libraryCount = LibraryDialogLoadAll(libraries, MAX_LIBRARIES);
    YTDLP_Init();
}

void UIUpdate(float deltaTime)
{
    /* Global playback tick — runs even while modals are open so that
       "next song" advances regardless of what page is visible. */
    LibraryManagerTick();

    float target = menuOpen ? 1.0f : 0.0f;
    menuProgress += (target - menuProgress) * (1.0f - expf(-11.0f * deltaTime));

    if (renameActive) { RenameUpdate(); return; }
    if (addSongActive) { AddSongUpdate(); return; }

    if (LibraryManagerIsOpen()) {
        LibraryManagerUpdate();
        return;
    }

    if (LibraryDialogIsOpen()) {
        char created[LIBRARY_NAME_SIZE];
        if (LibraryDialogUpdate(created, sizeof(created)) &&
            libraryCount < MAX_LIBRARIES) {
            snprintf(libraries[libraryCount],
                     sizeof(libraries[libraryCount]), "%s", created);
            libraryCount++;
        }
        return;
    }

    HandlePageShortcuts();

    int sw = GetScreenWidth();
    int sh = GetScreenHeight();

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
        CheckCollisionPointRec(GetMousePosition(), NormalizeRect(sw))) {
        YTDLP_SetNormalize(!YTDLP_GetNormalize());
        return;
    }

    if (!menuOpen) UpdatePlayerBar(sw, sh);

    if (page == PAGE_LIBRARY && !menuOpen && libraryCount > 0) {
        if (selectedLibrary >= libraryCount) selectedLibrary = libraryCount - 1;
        if (selectedLibrary < 0) selectedLibrary = 0;

        if (IsKeyPressed(KEY_DOWN))
            selectedLibrary = (selectedLibrary + 1) % libraryCount;
        if (IsKeyPressed(KEY_UP))
            selectedLibrary = (selectedLibrary + libraryCount - 1) % libraryCount;
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) {
            LibraryManagerOpen(libraries[selectedLibrary]);
            return;
        }
        if (IsKeyPressed(KEY_R)) { OpenRename(selectedLibrary); return; }
        if (IsKeyPressed(KEY_D) || IsKeyPressed(KEY_DELETE)) {
            DeleteLibrary(selectedLibrary);
            return;
        }

        Vector2 mouse = GetMousePosition();
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            for (int i = 0; i < libraryCount; i++) {
                Rectangle card = { 40, 160 + i * 58.0f, (float)sw - 80.0f, 46 };
                Rectangle openBtn   = { card.x + card.width - 210, card.y + 8, 60, 30 };
                Rectangle renameBtn = { card.x + card.width - 140, card.y + 8, 60, 30 };
                Rectangle deleteBtn = { card.x + card.width - 70,  card.y + 8, 60, 30 };
                Rectangle cardBody  = { card.x, card.y, card.width - 220, card.height };

                if (CheckCollisionPointRec(mouse, openBtn) ||
                    CheckCollisionPointRec(mouse, cardBody)) {
                    selectedLibrary = i;
                    LibraryManagerOpen(libraries[i]);
                    return;
                }
                if (CheckCollisionPointRec(mouse, renameBtn)) {
                    selectedLibrary = i; OpenRename(i); return;
                }
                if (CheckCollisionPointRec(mouse, deleteBtn)) {
                    DeleteLibrary(i); return;
                }
            }
        }
    }

    if (page == PAGE_SEARCH && !menuOpen) {
        SearchBoxUpdate();

        bool ctrl  = CtrlHeld();
        bool enterPressed = IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER);
        bool shiftHeld = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);

        SongResult results[MAX_RESULTS];
        int count = YTDLP_GetResults(results, MAX_RESULTS);

        if (ctrl && IsKeyPressed(KEY_A)) {
            if (count > 0 && !YTDLP_IsSearching()) {
                if (selectedResult >= count) selectedResult = count - 1;
                if (selectedResult < 0) selectedResult = 0;
                OpenAddSong(&results[selectedResult]);
            }
        }

        if (enterPressed && shiftHeld) {
            if (count > 0 && !YTDLP_IsSearching()) {
                if (selectedResult >= count) selectedResult = count - 1;
                if (selectedResult < 0) selectedResult = 0;
                SetNowPlaying(&results[selectedResult]);
            }
            SearchBoxClearSubmitted();
        } else if (SearchBoxIsSubmitted()) {
            StartSearch();
            count = YTDLP_GetResults(results, MAX_RESULTS);
        }

        Rectangle view = ResultsViewRect(sh);
        float maxScroll = ResultMaxScroll(count, view.height);

        if (CheckCollisionPointRec(GetMousePosition(), view))
            resultScroll -= GetMouseWheelMove() * 42.0f;
        else
            (void)GetMouseWheelMove();

        if (resultScroll < 0.0f) resultScroll = 0.0f;
        if (resultScroll > maxScroll) resultScroll = maxScroll;

        if (count > 0) {
            bool moved = false;
            if (!ctrl && IsKeyPressed(KEY_DOWN)) {
                selectedResult = (selectedResult + 1) % count; moved = true;
            }
            if (!ctrl && IsKeyPressed(KEY_UP)) {
                selectedResult = (selectedResult + count - 1) % count; moved = true;
            }
            if (selectedResult >= count) selectedResult = count - 1;
            if (moved) {
                float rowTop = selectedResult * 80.0f;
                if (rowTop < resultScroll) resultScroll = rowTop;
                if (rowTop + 72.0f > resultScroll + view.height)
                    resultScroll = rowTop + 72.0f - view.height;
                if (resultScroll < 0.0f) resultScroll = 0.0f;
                if (resultScroll > maxScroll) resultScroll = maxScroll;
            }
        }

        Rectangle thumb = ScrollThumb(view, count, sw);
        Rectangle track = ScrollTrack(view, sw);
        Vector2 mouse = GetMousePosition();

        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && maxScroll > 0.0f) {
            if (CheckCollisionPointRec(mouse, thumb)) {
                draggingScrollbar = true;
                scrollThumbGrabOffset = mouse.y - thumb.y;
            } else if (CheckCollisionPointRec(mouse, track)) {
                float ratio = (mouse.y - track.y) / track.height;
                if (ratio < 0.0f) ratio = 0.0f;
                if (ratio > 1.0f) ratio = 1.0f;
                resultScroll = ratio * maxScroll;
            }
        }

        if (draggingScrollbar) {
            if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                float thumbHeight = thumb.height;
                float travel = view.height - thumbHeight;
                float newY = mouse.y - scrollThumbGrabOffset - view.y;
                if (newY < 0.0f) newY = 0.0f;
                if (newY > travel) newY = travel;
                resultScroll = travel > 0.0f ? (newY / travel) * maxScroll : 0.0f;
            } else {
                draggingScrollbar = false;
            }
        }

        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            Rectangle searchButton = SearchButtonRect(sw);
            if (CheckCollisionPointRec(mouse, searchButton)) { StartSearch(); return; }

            if (CheckCollisionPointRec(mouse, view)) {
                for (int i = 0; i < count; i++) {
                    Rectangle row = { view.x,
                                      view.y + i * 80.0f - resultScroll,
                                      view.width, 72.0f };
                    Rectangle addBtn = { row.x + row.width - 120,
                                         row.y + 20, 70, 32 };
                    if (CheckCollisionPointRec(mouse, addBtn)) {
                        selectedResult = i;
                        OpenAddSong(&results[i]);
                        return;
                    }
                    if (CheckCollisionPointRec(mouse, row)) {
                        selectedResult = i;
                        SetNowPlaying(&results[i]);
                        return;
                    }
                }
            }
        }
    }

    if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return;

    Vector2 mouse = GetMousePosition();

    if (CheckCollisionPointRec(mouse, (Rectangle){ 20, 15, 42, 44 })) {
        menuOpen = !menuOpen;
        return;
    }

    if (menuProgress < 0.85f) return;

    if (CheckCollisionPointRec(mouse, (Rectangle){ 18, 100, 204, 46 })) {
        page = PAGE_HOME; menuOpen = false;
    } else if (CheckCollisionPointRec(mouse, (Rectangle){ 18, 154, 204, 46 })) {
        page = PAGE_SEARCH; menuOpen = false;
    } else if (CheckCollisionPointRec(mouse, (Rectangle){ 18, 208, 204, 46 })) {
        page = PAGE_LIBRARY; menuOpen = false;
    } else if (CheckCollisionPointRec(mouse, (Rectangle){ 18, 262, 204, 48 })) {
        LibraryDialogOpen(); menuOpen = false;
    } else if (menuOpen) {
        Rectangle drawerArea = { 0, 72, 240, (float)(sh - 72) };
        if (!CheckCollisionPointRec(mouse, drawerArea)) menuOpen = false;
    }
}

void UIDraw(void)
{
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();

    ClearBackground(BG);
    DrawMenuIcon();

    const char *brand = "EA-MUSIC";
    float brandWidth = TextWidth(brand, 19);
    Text(brand, (Vector2){ (sw - brandWidth) / 2.0f, 26 }, 19, UI_WHITE);
    DrawLine(0, 72, sw, 72, BORDER);
    DrawNormalizeToggle(sw);

    if (page == PAGE_HOME) {
        char shown[300];
        float size = 32;
        if (hasNowPlaying) {
            char full[300];
            snprintf(full, sizeof(full), "Playing %s", nowPlayingTitle);
            FitText(full, size, sw - 80.0f, shown, sizeof(shown));
        } else {
            snprintf(shown, sizeof(shown), "%s", messages[messageIndex]);
        }
        float width = TextWidth(shown, size);
        Text(shown, (Vector2){ (sw - width) / 2.0f, sh * 0.44f }, size, UI_WHITE);

        if (!hasNowPlaying) {
            const char *subtitle = "A little space for the music you love.";
            float subWidth = TextWidth(subtitle, 15);
            Text(subtitle, (Vector2){ (sw - subWidth) / 2.0f, sh * 0.44f + 52 },
                 15, MUTED);
        } else {
            const char *hint =
                "Space pause  |  S stop  |  L loop  |  M mute  |  N normalize  |  Left/Right seek  |  Up/Down volume";
            float hintWidth = TextWidth(hint, 13);
            Text(hint, (Vector2){ (sw - hintWidth) / 2.0f, sh * 0.44f + 52 },
                 13, MUTED);
        }
    } else if (page == PAGE_SEARCH) {
        Text("Search", (Vector2){ 40, 110 }, 26, UI_WHITE);
        SearchBoxDraw(sw);

        Rectangle button = SearchButtonRect(sw);
        DrawRectangleRounded(button, 0.2f, 8, ACCENT);
        const char *label = YTDLP_IsSearching() ? "Searching" : "Search";
        float labelWidth = TextWidth(label, 14);
        Text(label, (Vector2){ button.x + (button.width - labelWidth) / 2.0f,
                               button.y + 18.0f },
             14, (Color){ 24, 22, 31, 255 });

        DrawSearchResults(sw, sh);
    } else {
        Text("Library", (Vector2){ 40, 110 }, 26, UI_WHITE);
        if (libraryCount == 0) {
            Text("Nothing is over here yet.", (Vector2){ 40, 165 }, 16, MUTED);
        } else {
            for (int i = 0; i < libraryCount; i++) {
                Rectangle card = { 40, 160 + i * 58.0f, (float)sw - 80.0f, 46 };
                bool selected = (i == selectedLibrary);
                DrawRectangleRounded(card, 0.18f, 8,
                    selected ? (Color){ 39, 38, 53, 255 } : PANEL);
                Text(libraries[i], (Vector2){ card.x + 16, card.y + 13 },
                     16, UI_WHITE);

                Rectangle openBtn   = { card.x + card.width - 210, card.y + 8, 60, 30 };
                Rectangle renameBtn = { card.x + card.width - 140, card.y + 8, 60, 30 };
                Rectangle deleteBtn = { card.x + card.width - 70,  card.y + 8, 60, 30 };

                DrawRectangleRounded(openBtn, 0.2f, 6, ACCENT);
                Text("Open", (Vector2){ openBtn.x + 14, openBtn.y + 7 },
                     13, (Color){ 24, 22, 31, 255 });

                DrawRectangleRounded(renameBtn, 0.2f, 6, (Color){ 52, 56, 68, 255 });
                Text("Rename", (Vector2){ renameBtn.x + 8, renameBtn.y + 7 },
                     13, UI_WHITE);

                DrawRectangleRounded(deleteBtn, 0.2f, 6, (Color){ 63, 40, 48, 255 });
                Text("Delete", (Vector2){ deleteBtn.x + 12, deleteBtn.y + 7 },
                     13, (Color){ 255, 150, 150, 255 });
            }
        }
    }

    DrawPlayerBar(sw, sh);

    if (menuProgress > 0.01f) {
        float drawerWidth = 240.0f * menuProgress;
        unsigned char dimAlpha = (unsigned char)(130 * menuProgress);
        DrawRectangle(0, 72, sw, sh - 72, (Color){ 0, 0, 0, dimAlpha });
        DrawRectangle(0, 72, (int)drawerWidth, sh - 72, PANEL);

        BeginScissorMode(0, 72, (int)drawerWidth, sh - 72);
        Text("MENU", (Vector2){ 24, 82 }, 12, MUTED);

        DrawNavButton((Rectangle){ 18, 100, 204, 46 }, "Home",   page == PAGE_HOME);
        DrawNavButton((Rectangle){ 18, 154, 204, 46 }, "Search", page == PAGE_SEARCH);
        DrawNavButton((Rectangle){ 18, 208, 204, 46 }, "Library", page == PAGE_LIBRARY);

        Rectangle newButton = { 18, 262, 204, 48 };
        DrawRectangleRounded(newButton, 0.2f, 8, PANEL);
        Text("+", (Vector2){ 34, 272 }, 26, ACCENT);
        Text("New library", (Vector2){ 62, 277 }, 16, UI_WHITE);
        Text("Ctrl+1/2/3/4 · Ctrl+Tab", (Vector2){ 24, 326 }, 12, MUTED);
        EndScissorMode();
    }

    LibraryDialogDraw();
    LibraryManagerDraw(sw, sh);
    RenameDraw(sw, sh);
    AddSongDraw(sw, sh);
}

void UIClose(void)
{
    YTDLP_Cleanup();
    if (fontsLoaded) {
        for (int i = 0; i < FONT_SLOTS; i++) UnloadFont(fonts[i]);
    }
}
