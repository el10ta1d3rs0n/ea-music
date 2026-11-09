#include "search_box.h"
#include "../ui.h"

#include "raylib.h"
#include <math.h>
#include <string.h>

#define INPUT_SIZE 256

static char text[INPUT_SIZE];
static int length;
static bool submitted = false;
static double backspaceNext;

static void DeleteChar(void)
{
    if (length <= 0) return;

    length--;
    while (length > 0 && ((unsigned char)text[length] & 0xC0) == 0x80)
        length--; /* don't cut a UTF-8 character in half */
    text[length] = '\0';
}

static void DeleteWord(void)
{
    while (length > 0 && text[length - 1] == ' ') length--;
    while (length > 0 && text[length - 1] != ' ') length--;
    text[length] = '\0';
}

static void AppendCodepoint(int codepoint)
{
    int n = 0;
    const char *bytes = CodepointToUTF8(codepoint, &n);

    if (n <= 0 || length + n >= INPUT_SIZE) return;

    memcpy(text + length, bytes, (size_t)n);
    length += n;
    text[length] = '\0';
}

void SearchBoxUpdate(void)
{
    submitted = false;

    bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    double now = GetTime();

    /* Ctrl+A clears the box (as before). */
    if (ctrl && IsKeyPressed(KEY_A)) {
        text[0] = '\0';
        length = 0;
    }

    /* Typed characters. Skipped while Ctrl is held so shortcuts don't type. */
    int character;
    while ((character = GetCharPressed()) != 0) {
        if (!ctrl && character >= 32 && character != 127)
            AppendCodepoint(character);
    }

    /* Backspace / Delete: delete on press, then repeat while held. */
    bool del = IsKeyDown(KEY_BACKSPACE) || IsKeyDown(KEY_DELETE);
    bool delPressed = IsKeyPressed(KEY_BACKSPACE) || IsKeyPressed(KEY_DELETE);

    if (delPressed) {
        ctrl ? DeleteWord() : DeleteChar();
        backspaceNext = now + 0.40;
    } else if (del) {
        while (now >= backspaceNext) {
            ctrl ? DeleteWord() : DeleteChar();
            backspaceNext += 0.035;
        }
    }

    /* Ctrl+V paste */
    if (ctrl && IsKeyPressed(KEY_V)) {
        const char *clip = GetClipboardText();
        if (clip) {
            for (const char *p = clip; *p && length < INPUT_SIZE - 1; p++) {
                unsigned char ch = (unsigned char)*p;
                if (ch < 32 || ch == 127)
                    continue; /* skip newlines and control bytes */
                text[length++] = (char)ch;
            }
            text[length] = '\0';
        }
    }

    /* Enter searches — even an empty box, so the caller can reset
       the search page back to its default state.
       Shift+Enter is still reserved for "play highlighted". */
    if ((IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) && !shift) {
        submitted = true;
    }
}

const char *SearchBoxText(void)
{
    return text;
}

void SearchBoxDraw(int screenWidth)
{
    const float size = 15.0f;
    Font font = UIGetFontSized(size);

    const float left = 40.0f;
    const float top = 158.0f;
    const float rightPadding = 176.0f;
    float width = screenWidth - left - rightPadding;

    if (width < 150.0f) {
        width = 150.0f;
    }

    Rectangle box = { left, top, width, 52.0f };

    DrawRectangleRounded(box, 0.22f, 8, (Color){ 26, 29, 37, 255 });
    DrawRectangleRoundedLines(box, 0.22f, 8,
                              (Color){ 61, 66, 80, 255 });

    float innerW = box.width - 32.0f;
    float textW = text[0] ? MeasureTextEx(font, text, size, 0.0f).x : 0.0f;
    float scrollX = textW > innerW ? textW - innerW : 0.0f; /* long text scrolls */
    float ty = roundf(top + (box.height - size) / 2.0f);

    BeginScissorMode((int)box.x + 10, (int)box.y,
                     (int)box.width - 20, (int)box.height);

    if (text[0]) {
        DrawTextEx(font, text,
                   (Vector2){ roundf(left + 16.0f - scrollX), ty },
                   size, 0.0f, (Color){ 239, 241, 247, 255 });
    } else {
        DrawTextEx(font, "Search songs, artists, albums...",
                   (Vector2){ roundf(left + 16.0f), ty },
                   size, 0.0f, (Color){ 151, 158, 173, 255 });
    }

    if ((int)(GetTime() * 2.0) % 2 == 0) {
        float cursorX = roundf(left + 16.0f + textW - scrollX + 1.0f);
        DrawRectangle((int)cursorX, (int)top + 17, 2, 18,
                      (Color){ 161, 143, 255, 255 });
    }

    EndScissorMode();
}

bool SearchBoxIsSubmitted(void)
{
    return submitted;
}

void SearchBoxClearSubmitted(void)
{
    submitted = false;
}
