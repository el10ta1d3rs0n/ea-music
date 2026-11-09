#ifndef UI_H
#define UI_H

#include "raylib.h"

void UIInit(void);
void UIUpdate(float deltaTime);
void UIDraw(void);
void UIClose(void);

Font UIGetFont(void);
Font UIGetFontSized(float size);

void UISetNowPlaying(const char *title, const char *id);

#endif

