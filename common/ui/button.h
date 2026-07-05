#ifndef TS_BUTTON_H
#define TS_BUTTON_H

#include "fonts.h"
#include "string_view/string_view.h"

typedef void (*TwinStudio_HoverCallback)(Clay_ElementId, Clay_PointerData, void *);
typedef void (*TwinStudio_ClickCallback)(Clay_ElementId, Clay_PointerData, void *);


typedef struct TwinStudio_UiButtonDesc {
    TwinStudio_StringView id;
    TwinStudio_StringView label;
    TwinStudio_HoverCallback hoverCallback;
    TwinStudio_ClickCallback clickCallback;
    void* data;
    TwinStudio_Fonts font;
} TwinStudio_UiButtonDesc;


void TwinStudio_RenderButton(TwinStudio_UiButtonDesc* desc);


#endif // TS_BUTTON_H