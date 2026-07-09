#include "memory/memory.h"
#include "serialization/binary_serializer.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <raylib.h>
#include <rpmalloc.h>
#include <cJSON.h>
#include <clay.h>
#include <string.h>
#include <tinyfiledialogs.h>

#define TS_RENDERER_IMPLEMENTATION
#include "render/renderer.h"

#include "string_view/string_view.h"
#include "ecs/ecs.h"
#include "ui/ui.h"
#include "ui/textbox.h"
#include "ui/button.h"
#include "ui/fonts.h"
#include "ps2/retail/auto_struct_bh_archive.h"


void HandleClayErrors(Clay_ErrorData errorData)
{
    fprintf(stderr, "%s", errorData.errorText.chars);
}


void HandleButtonClick(Clay_ElementId element, Clay_PointerData pointerData, void* data)
{
    const char* filterPatterns[1];
    filterPatterns[0] = "*.BH";
    char* openedFile = tinyfd_openFileDialog("Select BH archive", NULL, 1, filterPatterns, "*.BH|Bandicoot Header", 0);
    if (openedFile == NULL)
    {
        fprintf(stderr, "No file opened!\n");
        return;
    }

    fprintf(stderr, "Opened %s\n", openedFile);
    TwinRes_BhArchive headerArchive = TwinRes_BhArchiveCreate();
    TwinStudio_Arena archiveArena = TwinStudio_CreateArena(1024 * 1024 * 5);
    TwinStudio_StringView filePath = TwinStudio_CopyFromCStringArena(&archiveArena, openedFile);
    TwinStudio_BinarySerializer* deserializer = TwinStudio_BinReadFromFile(filePath, false);
    TwinRes_BhArchiveBinDeserialize(&headerArchive, deserializer, &archiveArena, TwinStudio_BinGetStreamLength(deserializer), NULL);
    for (uint32_t i = 0; i < arrlen(headerArchive.records); ++i)
    {
        fprintf(stderr, "%d. "TS_VIEW_FORMAT" Size: %d\n", i + 1, TS_VIEW_ARG(headerArchive.records[i].path), headerArchive.records[i].length);
    }

    arrfree(headerArchive.records);
    TwinStudio_ArenaFree(&archiveArena);
    TwinStudio_BinSerializerFree(deserializer);
}


int main(int argc, char** argv)
{
    rpmalloc_initialize(NULL);

    Clay_Raylib_Initialize(1280, 720, "Twin Studio", FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_HIGHDPI | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);

    uint64_t clayRequiredMemory = Clay_MinMemorySize();
    Clay_Arena clayMemory = Clay_CreateArenaWithCapacityAndMemory(clayRequiredMemory, malloc(clayRequiredMemory));
    Clay_Initialize(clayMemory, (Clay_Dimensions) { .width = GetScreenWidth(), .height = GetScreenHeight() }, (Clay_ErrorHandler) { HandleClayErrors });

    TwinStudio_EcsInit();
    TwinStudio_UiInit();
    Clay_SetDebugModeEnabled(true);

    TwinStudio_UiButtonDesc testBtn = {
        .id = TS_STRING_VIEW("TEST_BUTTON"),
        .font = TwinStudio_FontInter,
        .label = TS_STRING_VIEW("Click Me!"),
        .clickCallback = HandleButtonClick
    };

    TwinStudio_StringView testEditStr = TS_STRING_VIEW("Test string");
    int testNumStr = 10;
    float testFloatStr = 2031.5912f;

    Clay_TextElementConfig textConfig = {
        .fontId = TwinStudio_FontInter,
        .fontSize = 16,
        .textColor = { 0, 0, 0, 255 }
    };

    TwinStudio_BinarySerializer* bdSerializer = TwinStudio_BinReadFromFile(TS_STRING_VIEW("/mnt/speed/Twinsanity Discs/Twins Full Extract PAL/Crash6/Crash.BD"), true);

    TwinStudio_TextBoxDesc textTextBox = {
        .id = TS_STRING_VIEW("TEST_TEXT_BOX"),
        .config = textConfig,
        .textConverter = TwinStudio_ConvertFloatToString,
        .backTextConverter = TwinStudio_ConvertBackStringToFloat,
        .maxChars = 100,
        .selectTextOnReceivingFocus = true,
        .data = &testFloatStr
    };

    TwinStudio_TextBoxDesc textTextBox2 = {
        .id = TS_STRING_VIEW("TEST_TEXT_BOX2"),
        .config = textConfig,
        .textConverter = TwinStudio_ConvertStringToString,
        .maxChars = 100,
        .data = &testEditStr
    };

    TwinStudio_TextBoxDesc textTextBox3 = {
        .id = TS_STRING_VIEW("TEST_TEXT_BOX3"),
        .config = textConfig,
        .textConverter = TwinStudio_ConvertIntToString,
        .backTextConverter = TwinStudio_ConvertBackStringToInt,
        .maxChars = 100,
        .data = &testNumStr
    };

    TwinStudio_TextBoxDesc textTextBox4 = {
        .id = TS_STRING_VIEW("TEST_TEXT_BOX4"),
        .config = textConfig,
        .textConverter = TwinStudio_ConvertIntToString,
        .backTextConverter = TwinStudio_ConvertBackStringToInt,
        .maxChars = 100,
        .data = &testNumStr
    };

    TwinStudio_TextboxInit(&textTextBox);
    TwinStudio_TextboxInit(&textTextBox2);
    TwinStudio_TextboxInit(&textTextBox3);
    TwinStudio_TextboxInit(&textTextBox4);

    while(!WindowShouldClose())
    {
        const float dt = GetFrameTime();
        Clay_SetLayoutDimensions((Clay_Dimensions) {
            .width = GetScreenWidth(),
            .height = GetScreenHeight()
        });

        Vector2 scrollDelta = GetMouseWheelMoveV();
        Clay_SetPointerState((Clay_Vector2) { .x = GetMousePosition().x, .y = GetMousePosition().y }, IsMouseButtonDown(MOUSE_LEFT_BUTTON));
        Clay_UpdateScrollContainers(false, (Clay_Vector2) { .x = scrollDelta.x * 3.0f, .y = scrollDelta.y * 3.0f }, dt);

        TwinStudio_UiUpdateTimers(dt);
        TwinStudio_UiUpdateInput();

        char fpsBuffer[64];

        Clay_BeginLayout();

        CLAY(CLAY_ID("MainWindow"), {
            .backgroundColor = { 128, 128, 128, 255 },
            .layout = {
                .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0) },
                .layoutDirection = CLAY_TOP_TO_BOTTOM,
                .padding = CLAY_PADDING_ALL(16),
                .childGap = 16,
            }
        }) {
            float fps = 1.0f / dt;
            int charsSize = snprintf(fpsBuffer, sizeof(fpsBuffer), "FPS: %d", (int)fps);
            Clay_String fpsStr = { .isStaticallyAllocated = false, .length = charsSize, .chars = fpsBuffer };
            CLAY_TEXT(fpsStr, {
                .textColor = { 255, 255, 255, 255 },
                .fontSize = 24,
                .fontId = TwinStudio_FontTwin,
            });

            TwinStudio_RenderButton(&testBtn);
            TwinStudio_TextboxRender(&textTextBox);
            TwinStudio_TextboxRender(&textTextBox2);
            TwinStudio_TextboxRender(&textTextBox3);
            TwinStudio_TextboxRender(&textTextBox4);
        }

        Clay_RenderCommandArray renderCommands = Clay_EndLayout(dt);

        BeginDrawing();
        ClearBackground(BLACK);
        Clay_Raylib_Render(renderCommands, TwinStudio_GetUiContext()->fonts);
        EndDrawing();
    }

    Clay_Raylib_Close();
    rpmalloc_finalize();

    return 0;
}