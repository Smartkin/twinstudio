#include "ui.h"
#include "string_view/string_view.h"
#include "timer/timer.h"
#include "ui/textbox.h"
#include "render/renderer.h"
#include <stdbool.h>
#include <raylib.h>
#include <string.h>


const TwinStudio_UiContext* gUiContext;
TwinStudio_UiContext context;


static void HandleTickVisibility()
{
    context.isTextboxTickVisible = !context.isTextboxTickVisible;
}

TwinStudio_Timer tickTimer;


void TwinStudio_UiInit()
{
    gUiContext = &context;
    tickTimer = TwinStudio_CreateTimer(0.5f, true, HandleTickVisibility);

    context.fonts[TwinStudio_FontInter] = LoadFontEx("resources/fonts/InterDisplay-Regular.ttf", 24, NULL, 400);
    context.fonts[TwinStudio_FontTwin]  = LoadFontEx("resources/fonts/Twelve-Ton-Goldfish.ttf", 24, NULL, 400);
    SetTextureFilter(context.fonts[TwinStudio_FontInter].texture, TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(context.fonts[TwinStudio_FontTwin].texture, TEXTURE_FILTER_BILINEAR);
    Clay_SetMeasureTextFunction(Raylib_MeasureText, context.fonts);
}


void TwinStudio_UiChangeActiveTextbox(TwinStudio_TextBoxDesc* textbox)
{
    if (context.selectedTextBox != NULL)
    {
        TwinStudio_TextboxCommitData(context.selectedTextBox);
    }

    context.selectedTextBox = textbox;
    // See TwinStudio_UiContext::textboxJustFocused's comment - only
    // meaningful when switching TO a real textbox (the only caller that
    // does is HandleClickTextbox, in response to the click that's about to
    // be processed by UpdateCurrentTextBox later this same frame).
    context.textboxJustFocused = (textbox != NULL);
}


TwinStudio_UiContext* TwinStudio_GetUiContext()
{
    return &context;
}


void TwinStudio_UiUpdateTimers(float dt)
{
    TwinStudio_UpdateTimer(&tickTimer, dt);
}


static void UpdateCurrentTextBox()
{
    if (context.selectedTextBox == NULL)
    {
        return;
    }

    const bool isShiftPressed = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    const bool isCtrlPressed = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);

    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
    {
        if (context.textboxJustFocused)
        {
            // HandleClickTextbox already set up the right selection for
            // this exact click earlier in the frame (see
            // TwinStudio_UiContext::textboxJustFocused) - don't immediately
            // wipe it.
            context.textboxJustFocused = false;
        }
        else
        {
            TwinStudio_TextboxResetSelection(context.selectedTextBox);
        }
    }

    if (!isShiftPressed && !isCtrlPressed)
    {
        int key = GetCharPressed();
        if (key > 0)
        {
            TwinStudio_TextboxDeleteSelection(context.selectedTextBox);
        }
        while (key > 0)
        {
            if ((key >= 32) && (key <= 125) && (context.selectedTextBox->maxChars == 0 || context.selectedTextBox->stringCache->length < context.selectedTextBox->maxChars)) {
                char c = (char)key;
                TwinStudio_TextboxInsertString(context.selectedTextBox, context.selectedTextBox->tickIndex, (TwinStudio_StringView) { .string = &c, .length = 1 });
            }

            key = GetCharPressed();
        }
    }

    if (isCtrlPressed && IsKeyPressed(KEY_A))
    {
        TwinStudio_TextboxSelectAll(context.selectedTextBox);
    }

    if (isCtrlPressed && IsKeyPressed(KEY_C) && TwinStudio_TextboxHasSelection(context.selectedTextBox))
    {
        TwinStudio_StringView* selectionString = TwinStudio_TextboxGetSelectedString(context.selectedTextBox);
        SetClipboardText(TwinStudio_GetCString(selectionString));
        TwinStudio_FreeString(selectionString);
    }

    if (isCtrlPressed && IsKeyPressed(KEY_V))
    {
        if (TwinStudio_TextboxHasSelection(context.selectedTextBox))
        {
            TwinStudio_TextboxDeleteSelection(context.selectedTextBox);
        }

        const char* clipboard = GetClipboardText();
        if (clipboard)
        {
            TwinStudio_TextboxInsertString(context.selectedTextBox, context.selectedTextBox->tickIndex, (TwinStudio_StringView) { .string = clipboard, .length = strlen(clipboard) });
        }
    }

    if (IsKeyPressed(KEY_LEFT))
    {
        if (!isShiftPressed)
        {
            TwinStudio_TextboxResetSelection(context.selectedTextBox);
        }
        else
        {
            TwinStudio_TextboxExpandSelectionLeft(context.selectedTextBox, 1);
        }

        if (context.selectedTextBox->tickIndex > 0)
        {
            TwinStudio_TextboxSetTickIndex(context.selectedTextBox, context.selectedTextBox->tickIndex - 1);
        }
    }

    if (IsKeyPressed(KEY_RIGHT))
    {
        if (!isShiftPressed)
        {
            TwinStudio_TextboxResetSelection(context.selectedTextBox);
        }
        else
        {
            TwinStudio_TextboxExpandSelectionRight(context.selectedTextBox, 1);
        }

        TwinStudio_TextboxSetTickIndex(context.selectedTextBox, context.selectedTextBox->tickIndex + 1);
    }

    if (IsKeyPressed(KEY_BACKSPACE))
    {
        if (TwinStudio_TextboxHasSelection(context.selectedTextBox))
        {
            TwinStudio_TextboxDeleteSelection(context.selectedTextBox);
        }
        else
        {
            TwinStudio_TextboxDeleteCharactersLeft(context.selectedTextBox, context.selectedTextBox->tickIndex, 1);
        }
    }

    if (IsKeyPressed(KEY_DELETE))
    {
        if (TwinStudio_TextboxHasSelection(context.selectedTextBox))
        {
            TwinStudio_TextboxDeleteSelection(context.selectedTextBox);
        }
        else
        {
            TwinStudio_TextboxDeleteCharactersRight(context.selectedTextBox, context.selectedTextBox->tickIndex, 1);
        }
    }

    if (IsKeyPressed(KEY_ENTER))
    {
        TwinStudio_TextboxCommitData(context.selectedTextBox);
        context.selectedTextBox = NULL;
    }
}


void TwinStudio_UiUpdateInput()
{
    UpdateCurrentTextBox();
}


