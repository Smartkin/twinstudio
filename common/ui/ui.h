#ifndef TS_COMMON_UI_H
#define TS_COMMON_UI_H

#include "textbox.h"
#include "fonts.h"
#include <clay.h>
#include <raylib.h>


typedef struct TwinStudio_UiContext {
    Font fonts[TwinStudio_FontsTotal];
    TwinStudio_TextBoxDesc* selectedTextBox;
    bool isTextboxTickVisible;
} TwinStudio_UiContext;


void TwinStudio_UiInit();
void TwinStudio_UiUpdateTimers(float dt);
void TwinStudio_UiUpdateInput();
void TwinStudio_UiChangeActiveTextbox(TwinStudio_TextBoxDesc* textbox);
TwinStudio_UiContext* TwinStudio_GetUiContext(); // Explicitly get UI context as a mutable pointer. If not needed prefer using const pointer global


extern const TwinStudio_UiContext* gUiContext;


#endif // TS_COMMON_UI_H