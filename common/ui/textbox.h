#ifndef TS_TEXTBOX_H
#define TS_TEXTBOX_H

#include "memory/memory.h"
#include "string_view/string_view.h"

#include <clay.h>
#include <stdint.h>


typedef TwinStudio_StringView* (*TwinStudio_TextConverter)(void*);
typedef void (*TwinStudio_TextBackConverter)(void*, TwinStudio_StringView*);


typedef struct TwinStudio_TextBoxSelection {
    uint32_t startSelection;
    uint32_t endSelection;
} TwinStudio_TextBoxSelection;

typedef struct TwinStudio_TextBoxDesc {
    TwinStudio_StringView id;
    void* data;
    TwinStudio_Arena arena;
    TwinStudio_TextConverter textConverter;
    TwinStudio_TextBackConverter backTextConverter;
    TwinStudio_StringView* stringCache;
    uint32_t maxChars;
    uint32_t tickIndex;
    TwinStudio_TextBoxSelection currentSelection;
    Clay_TextElementConfig config;
    Clay_Padding padding;
    bool selectTextOnReceivingFocus;

    // The field's own background, independent of desc->config.textColor.
    // Left at {0} (fully transparent, alpha 0 - never a sensible field
    // background on its own), TwinStudio_TextboxRender falls back to its
    // historical hardcoded gray, so existing callers that never set this
    // keep their current appearance; a caller matching a specific theme
    // (e.g. a dark UI) sets it explicitly instead.
    Clay_Color backgroundColor;
} TwinStudio_TextBoxDesc;


void TwinStudio_TextboxInit(TwinStudio_TextBoxDesc* desc, TwinStudio_Arena* arena);
void TwinStudio_TextboxInvalidateData(TwinStudio_TextBoxDesc* desc);
void TwinStudio_TextboxCommitData(TwinStudio_TextBoxDesc* desc);
void TwinStudio_TextboxInsertString(TwinStudio_TextBoxDesc* desc, uint32_t index, TwinStudio_StringView string);
void TwinStudio_TextboxDeleteCharactersLeft(TwinStudio_TextBoxDesc* desc, uint32_t index, uint32_t length);
void TwinStudio_TextboxDeleteCharactersRight(TwinStudio_TextBoxDesc* desc, uint32_t index, uint32_t length);
void TwinStudio_TextboxSetTickIndex(TwinStudio_TextBoxDesc* desc, uint32_t newTickIndex);
void TwinStudio_TextboxSelectAll(TwinStudio_TextBoxDesc* desc);
bool TwinStudio_TextboxHasSelection(TwinStudio_TextBoxDesc* desc);
TwinStudio_StringView* TwinStudio_TextboxGetSelectedString(TwinStudio_TextBoxDesc* desc); // Generated string is dynamically allocated. Free somewhere after this call!
void TwinStudio_TextboxExpandSelectionLeft(TwinStudio_TextBoxDesc* desc, uint32_t length);
void TwinStudio_TextboxExpandSelectionRight(TwinStudio_TextBoxDesc* desc, uint32_t length);
void TwinStudio_TextboxResetSelection(TwinStudio_TextBoxDesc* desc);
void TwinStudio_TextboxDeleteSelection(TwinStudio_TextBoxDesc* desc);
void TwinStudio_TextboxRender(TwinStudio_TextBoxDesc* desc);


#endif // TS_TEXTBOX_H