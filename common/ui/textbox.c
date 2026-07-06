#include "textbox.h"
#include "rpmalloc.h"
#include "math/math.h"
#include "string_view/string_view.h"
#include "render/renderer.h"
#include "ui/ui.h"
#include <stdint.h>
#include <string.h>
#include <raylib.h>
#include <assert.h>


static void CacheTextboxString(TwinStudio_TextBoxDesc* desc)
{
    desc->stringCache = desc->textConverter(desc->data);
}


static TwinStudio_StringView GetStringFromTextBox(TwinStudio_TextBoxDesc* desc)
{
    if (desc->stringCache == NULL)
    {
        CacheTextboxString(desc);
    }

    return *desc->stringCache;
}


static int32_t GetCharacterClicked(TwinStudio_TextBoxDesc* desc, Clay_Vector2 clickPosition)
{
    Clay_Vector2 attachOffset =  { .x = desc->padding.left, .y = desc->padding.top };
    if (clickPosition.x <= attachOffset.x)
    {
        return 0;
    }

    Clay_String cachedText = TS_STRING_TO_CLAY(GetStringFromTextBox(desc));
    Clay_StringSlice textSlice = { .length = cachedText.length, .chars = cachedText.chars, .baseChars = cachedText.chars };
    Clay_Dimensions textDims = Raylib_MeasureText(textSlice, &desc->config, &TwinStudio_GetUiContext()->fonts[desc->config.fontId]);
    attachOffset.x += textDims.width;
    if (clickPosition.x >= attachOffset.x)
    {
        return cachedText.length;
    }

    for (int32_t i = 1; i < cachedText.length; ++i)
    {
        Clay_StringSlice textSlice = { .length = i, .chars = cachedText.chars, .baseChars = cachedText.chars };
        Clay_Dimensions textDims = Raylib_MeasureText(textSlice, &desc->config, &TwinStudio_GetUiContext()->fonts[desc->config.fontId]);
        attachOffset.x = textDims.width + desc->padding.left;
        if (clickPosition.x <= attachOffset.x)
        {
            return i;
        }
    }

    return cachedText.length;
}

static void HandleClickTextbox(Clay_ElementId element, Clay_PointerData pointerData, void* data)
{
    TwinStudio_TextBoxDesc* textBox = data;
    if (pointerData.state != CLAY_POINTER_DATA_PRESSED_THIS_FRAME)
    {
        return;
    }

    if (textBox != gUiContext->selectedTextBox)
    {
        if (textBox->selectTextOnReceivingFocus) {
            textBox->currentSelection.startSelection = 0;
            textBox->currentSelection.endSelection = textBox->stringCache->length;
        }

        TwinStudio_UiChangeActiveTextbox(textBox);
    }
    else
    {
        Clay_BoundingBox textboxBb = Clay_GetElementData(element).boundingBox;
        Clay_Vector2 textboxPosition = { .x = textboxBb.x, .y = textboxBb.y };
        TwinStudio_TextboxResetSelection(textBox);
        int32_t newTickIndex = GetCharacterClicked(textBox, (Clay_Vector2) { .x = pointerData.position.x - textboxPosition.x, .y = pointerData.position.y - textboxPosition.y});
        TwinStudio_TextboxSetTickIndex(textBox, newTickIndex);
    }
}


void TwinStudio_TextboxInit(TwinStudio_TextBoxDesc* desc)
{
    assert(desc->id.string);
    assert(desc->textConverter);
    assert(desc->data);

    CacheTextboxString(desc);
    if (desc->config.fontSize == 0)
    {
        desc->config.fontSize = 16;
    }
    desc->tickIndex = desc->stringCache->length;
}


void TwinStudio_TextboxInsertString(TwinStudio_TextBoxDesc* desc, uint32_t index, TwinStudio_StringView string)
{
    if (desc->stringCache == NULL)
    {
        return;
    }

    uint32_t insertedLength = string.length;
    if (desc->maxChars > 0 && insertedLength + desc->stringCache->length > desc->maxChars)
    {
        insertedLength = desc->maxChars - desc->stringCache->length;
    }

    if (insertedLength == 0)
    {
        return;
    }

    if (desc->stringCache->allocatedLength < desc->stringCache->length + insertedLength)
    {
        desc->stringCache->dynString = rprealloc(desc->stringCache->dynString, (desc->stringCache->allocatedLength + insertedLength) * 2);
        desc->stringCache->allocatedLength += insertedLength;
        desc->stringCache->allocatedLength *= 2;
    }

    if (index < desc->stringCache->length)
    {
        memmove(desc->stringCache->dynString + index + insertedLength, desc->stringCache->dynString + index, desc->stringCache->length - index);
    }
    desc->stringCache->length += insertedLength;
    memcpy(desc->stringCache->dynString + index, string.string, insertedLength);

    TwinStudio_TextboxSetTickIndex(desc, desc->tickIndex + insertedLength);
}


void TwinStudio_TextboxDeleteCharactersLeft(TwinStudio_TextBoxDesc* desc, uint32_t index, uint32_t length)
{
    if (desc->stringCache == NULL || desc->stringCache->length <= 0 || index <= 0)
    {
        return;
    }

    if (index >= desc->stringCache->length)
    {
        const uint32_t deleteLength = TwinStudio_MinUInt(length, desc->stringCache->length);
        desc->stringCache->length -= deleteLength;
        memset(desc->stringCache->dynString + desc->stringCache->length, 0, deleteLength);
        TwinStudio_TextboxSetTickIndex(desc, desc->tickIndex);
        return;
    }

    const uint32_t passedIndex = index;
    const uint32_t deleteIndex = length > passedIndex ? 0 : passedIndex - length;

    TwinStudio_TextboxSetTickIndex(desc, deleteIndex);
    TwinStudio_TextboxDeleteCharactersRight(desc, deleteIndex, length);
}


void TwinStudio_TextboxDeleteCharactersRight(TwinStudio_TextBoxDesc* desc, uint32_t index, uint32_t length)
{
    if (desc->stringCache == NULL || desc->stringCache->length <= 0 || index >= desc->stringCache->length)
    {
        return;
    }

    const uint32_t originalLength = desc->stringCache->length;
    const uint32_t deleteLength = TwinStudio_MinUInt(length, desc->stringCache->length);

    memset(desc->stringCache->dynString + index, 0, deleteLength);
    if (length + index < originalLength)
    {
        memmove(desc->stringCache->dynString + index, desc->stringCache->dynString + length + index, originalLength - (length + index));
    }

    desc->stringCache->length -= deleteLength;

    // Clamp tick index to new length if needed
    TwinStudio_TextboxSetTickIndex(desc, desc->tickIndex);
}


void TwinStudio_TextboxSetTickIndex(TwinStudio_TextBoxDesc* desc, uint32_t newTickIndex)
{
    desc->tickIndex = newTickIndex;
    if (desc->tickIndex > desc->stringCache->length)
    {
        desc->tickIndex = desc->stringCache->length;
    }
}


void TwinStudio_TextboxSelectAll(TwinStudio_TextBoxDesc* desc)
{
    desc->currentSelection.startSelection = 0;
    desc->currentSelection.endSelection = desc->stringCache->length;
}

bool TwinStudio_TextboxHasSelection(TwinStudio_TextBoxDesc* desc)
{
    const int32_t startSelection = TwinStudio_MinInt(desc->currentSelection.startSelection, desc->currentSelection.endSelection);
    const int32_t endSelection = TwinStudio_MaxInt(desc->currentSelection.startSelection, desc->currentSelection.endSelection);
    const int32_t selectionLength = endSelection - startSelection;
    return selectionLength > 0;
}


TwinStudio_StringView* TwinStudio_TextboxGetSelectedString(TwinStudio_TextBoxDesc* desc)
{
    assert(TwinStudio_TextboxHasSelection(desc));

    const int32_t startSelection = TwinStudio_MinInt(desc->currentSelection.startSelection, desc->currentSelection.endSelection);
    const int32_t endSelection = TwinStudio_MaxInt(desc->currentSelection.startSelection, desc->currentSelection.endSelection);
    const int32_t selectionLength = endSelection - startSelection;

    TwinStudio_StringView stringSlice = { .length = selectionLength, .string = desc->stringCache->dynString + startSelection };
    return TwinStudio_ConvertStringToString(&stringSlice);
}


void TwinStudio_TextboxExpandSelectionLeft(TwinStudio_TextBoxDesc* desc, uint32_t length)
{
    if (!TwinStudio_TextboxHasSelection(desc))
    {
        desc->currentSelection.startSelection = desc->tickIndex;
        desc->currentSelection.endSelection = desc->tickIndex;
    }

    if (desc->tickIndex > 0)
    {
        desc->currentSelection.startSelection = desc->tickIndex - 1;
    }
}


void TwinStudio_TextboxExpandSelectionRight(TwinStudio_TextBoxDesc* desc, uint32_t length)
{
    if (!TwinStudio_TextboxHasSelection(desc))
    {
        desc->currentSelection.startSelection = desc->tickIndex;
        desc->currentSelection.endSelection = desc->tickIndex;
    }

    desc->currentSelection.endSelection = desc->tickIndex + 1;
    if (desc->currentSelection.endSelection > desc->stringCache->length)
    {
        desc->currentSelection.endSelection = desc->stringCache->length;
    }
}


void TwinStudio_TextboxResetSelection(TwinStudio_TextBoxDesc* desc)
{
    desc->currentSelection.startSelection = 0;
    desc->currentSelection.endSelection = 0;
}


void TwinStudio_TextboxDeleteSelection(TwinStudio_TextBoxDesc* desc)
{
    const int32_t startSelection = TwinStudio_MinInt(desc->currentSelection.startSelection, desc->currentSelection.endSelection);
    const int32_t endSelection = TwinStudio_MaxInt(desc->currentSelection.startSelection, desc->currentSelection.endSelection);
    const int32_t selectionLength = endSelection - startSelection;
    if (selectionLength <= 0)
    {
        return;
    }

    TwinStudio_TextboxDeleteCharactersLeft(desc, endSelection, selectionLength);
    TwinStudio_TextboxResetSelection(desc);
}


void TwinStudio_TextboxFreeOwnedData(TwinStudio_TextBoxDesc* desc)
{
    TwinStudio_FreeString(desc->stringCache);
    desc->stringCache = NULL;
}


void TwinStudio_TextboxInvalidateData(TwinStudio_TextBoxDesc* desc)
{
    if (desc->stringCache == NULL)
    {
        return;
    }

    TwinStudio_TextboxFreeOwnedData(desc);
    CacheTextboxString(desc);
}


void TwinStudio_TextboxCommitData(TwinStudio_TextBoxDesc* desc)
{
    if (desc->backTextConverter == NULL || desc->stringCache->length <= 0 || desc->stringCache == NULL)
    {
        if (desc->stringCache != NULL)
        {
            TwinStudio_TextboxFreeOwnedData(desc);
        }

        TwinStudio_TextboxInit(desc);
        return;
    }

    desc->backTextConverter(desc->data, desc->stringCache);
    TwinStudio_TextboxInvalidateData(desc);
    TwinStudio_TextboxSetTickIndex(desc, desc->tickIndex);
}


static void RenderTextboxTick(TwinStudio_TextBoxDesc* desc, Clay_TextElementConfig* editTextConfig)
{
    Clay_Vector2 attachOffset =  { .x = desc->padding.left, .y = desc->padding.top };
    Clay_String cachedText = TS_STRING_TO_CLAY(GetStringFromTextBox(desc));
    if (desc->tickIndex > 0) {
        Clay_StringSlice textSlice = { .length = desc->tickIndex, .chars = cachedText.chars, .baseChars = cachedText.chars };
        Clay_Dimensions textDims = Raylib_MeasureText(textSlice, editTextConfig, &TwinStudio_GetUiContext()->fonts[editTextConfig->fontId]);
        attachOffset.x += textDims.width;
    }

    // Render the tick/caret nicer in the middle between characters
    const char tickStr[] = "|";
    const Clay_StringSlice tickSlice = { .length = 1, .chars = tickStr, .baseChars = tickStr };
    Clay_Dimensions tickDims = Raylib_MeasureText(tickSlice, editTextConfig, &TwinStudio_GetUiContext()->fonts[editTextConfig->fontId]);
    attachOffset.x -= tickDims.width / 2.0f;

    CLAY_AUTO_ID({
        .floating = {
            .attachTo = CLAY_ATTACH_TO_PARENT,
            .offset = attachOffset
        }
    }) {
        CLAY_TEXT(CLAY_STRING("|"), desc->config);
    }
}


static void RenderSelection(TwinStudio_TextBoxDesc* desc, Clay_TextElementConfig* editTextConfig)
{
    const int32_t startSelection = TwinStudio_MinInt(desc->currentSelection.startSelection, desc->currentSelection.endSelection);
    const int32_t endSelection = TwinStudio_MaxInt(desc->currentSelection.startSelection, desc->currentSelection.endSelection);
    const int32_t selectionLength = endSelection - startSelection;
    if (selectionLength <= 0)
    {
        return;
    }

    Clay_Vector2 attachOffset = { .x = desc->padding.left, .y = desc->padding.top };
    Clay_String cachedText = TS_STRING_TO_CLAY(GetStringFromTextBox(desc));
    Clay_StringSlice textSlice = { .length = startSelection, .chars = cachedText.chars, .baseChars = cachedText.chars };
    Clay_Dimensions textDims = Raylib_MeasureText(textSlice, editTextConfig, &TwinStudio_GetUiContext()->fonts[editTextConfig->fontId]);
    attachOffset.x += textDims.width;

    const char tickStr[] = "|";
    const Clay_StringSlice tickSlice = { .length = 1, .chars = tickStr, .baseChars = tickStr };
    Clay_Dimensions tickDims = Raylib_MeasureText(tickSlice, editTextConfig, &TwinStudio_GetUiContext()->fonts[editTextConfig->fontId]);
    attachOffset.x -= tickDims.width / 2.0f;

    Clay_Vector2 endAttachSize = { 0, 0 };
    Clay_StringSlice endTextSlice = { .length = selectionLength, .chars = cachedText.chars + startSelection, .baseChars = cachedText.chars };
    Clay_Dimensions endTextDims = Raylib_MeasureText(endTextSlice, editTextConfig, &TwinStudio_GetUiContext()->fonts[editTextConfig->fontId]);
    endAttachSize.x += endTextDims.width + tickDims.width;
    endAttachSize.y += endTextDims.height;

    CLAY_AUTO_ID({
        .floating = {
            .attachTo = CLAY_ATTACH_TO_PARENT,
            .offset = attachOffset
        },
        .layout = {
            .sizing = { .width = endAttachSize.x, .height = endAttachSize.y }
        },
        .backgroundColor = { 0, 0, 190, 125 }
    });
}


void TwinStudio_TextboxRender(TwinStudio_TextBoxDesc* desc)
{
    CLAY(CLAY_SID(TS_STRING_TO_CLAY(desc->id)), {
        .layout = {
            .padding = desc->padding,
            .layoutDirection = CLAY_LEFT_TO_RIGHT,
            .sizing = { .width = CLAY_SIZING_GROW(0) },
        },
        .backgroundColor = { 150, 150, 150, 255 }
    })
    {
        Clay_OnHover(HandleClickTextbox, desc);
        CLAY_TEXT(TS_STRING_TO_CLAY(GetStringFromTextBox(desc)), desc->config);

        if (desc == gUiContext->selectedTextBox)
        {
            if (gUiContext->isTextboxTickVisible)
            {
                RenderTextboxTick(desc, &desc->config);
            }
            
            RenderSelection(desc, &desc->config);
        }
    }
}