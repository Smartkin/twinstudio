#include "button.h"


static void HandleButtonHoverAndClick(Clay_ElementId element, Clay_PointerData pointerData, void* data)
{
    TwinStudio_UiButtonDesc* buttonDesc = data;
    if (buttonDesc->hoverCallback != NULL) {
        buttonDesc->hoverCallback(element, pointerData, buttonDesc->data);
    }
    
    if (pointerData.state == CLAY_POINTER_DATA_PRESSED_THIS_FRAME && buttonDesc->clickCallback != NULL)
    {
        buttonDesc->clickCallback(element, pointerData, buttonDesc->data);
    }
}


void TwinStudio_RenderButton(TwinStudio_UiButtonDesc* desc)
{
    CLAY(CLAY_SID(TS_STRING_TO_CLAY(desc->id)), {
        .layout = {
            .padding = { 16, 16, 8, 8 },
        },
        .backgroundColor = { 110, 110, 110, (float)(Clay_Hovered() ? 255 : 0) }
    })
    {
        Clay_OnHover(HandleButtonHoverAndClick, desc);
        CLAY_TEXT(TS_STRING_TO_CLAY(desc->label), {
            .fontId = desc->font,
            .fontSize = 16,
            .textColor = { 255, 255, 255, 255 }
        });
    }
}