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

    // True for the one UpdateCurrentTextBox call right after a click just
    // focused selectedTextBox (set by TwinStudio_UiChangeActiveTextbox,
    // consumed and cleared by UpdateCurrentTextBox) - lets that call skip
    // its own blanket "any click resets selection" handling for this one
    // click, since HandleClickTextbox (textbox.c) already set up the
    // correct selection for a freshly-focused box (all of it, if
    // selectTextOnReceivingFocus) earlier in the same frame; without this,
    // that selection was immediately wiped before the person could type
    // over it, so typing right after a click appended instead of replacing.
    bool textboxJustFocused;
} TwinStudio_UiContext;


void TwinStudio_UiInit();
void TwinStudio_UiUpdateTimers(float dt);
void TwinStudio_UiUpdateInput();
void TwinStudio_UiChangeActiveTextbox(TwinStudio_TextBoxDesc* textbox);
TwinStudio_UiContext* TwinStudio_GetUiContext(); // Explicitly get UI context as a mutable pointer. If not needed prefer using const pointer global


extern const TwinStudio_UiContext* gUiContext;


#endif // TS_COMMON_UI_H