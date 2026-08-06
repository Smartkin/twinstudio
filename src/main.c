#include "audio/wave.h"
#include "memory/memory.h"
#include "ps2/retail/archive_serializers.h"
#include "ps2/retail/auto_struct_bd_archive.h"
#include "ps2/retail/auto_struct_chunk.h"
#include "ps2/retail/auto_struct_mb_archive.h"
#include "ps2/retail/chunk_serializer.h"
#include "ps2/retail/graphics/auto_struct_blend_skin.h"
#include "ps2/retail/graphics/auto_struct_skin.h"
#include "ps2/retail/graphics/texture_serialization.h"
#include "ps2/vif.h"
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
#include "ps2/retail/auto_struct_mh_archive.h"
#include "ps2/retail/auto_struct_bh_archive.h"


void HandleClayErrors(Clay_ErrorData errorData)
{
    fprintf(stderr, "%s", errorData.errorText.chars);
}


void UnpackMusicArchives()
{
    const char* filterPatterns[1];
    filterPatterns[0] = "*.MH";
    char* openedFile = tinyfd_openFileDialog("Select MH archive", NULL, 1, filterPatterns, "*.MH Music Header", 0);
    if (openedFile == NULL)
    {
        fprintf(stderr, "No file opened!\n");
        return;
    }

    fprintf(stderr, "Opened %s\n", openedFile);
    TwinRes_MhArchive headerArchive = TwinRes_MhArchiveCreate();
    TwinStudio_Arena archiveArena = TwinStudio_CreateArena(1024UL * 1024UL * 200UL);
    TwinStudio_StringView filePath = TwinStudio_CopyFromCStringArena(&archiveArena, openedFile);
    TwinStudio_BinarySerializer* deserializer = TwinStudio_BinReadFromFile(filePath, false);
    TwinRes_MhArchiveBinDeserialize(&headerArchive, deserializer, &archiveArena, TwinStudio_BinGetStreamLength(deserializer), NULL);
    for (uint32_t i = 0; i < headerArchive.recordsAmount; ++i)
    {
        fprintf(stderr, "Track %d. Size: %d\n", i + 1, headerArchive.records[i].size);
    }

    TwinRes_MbArchive dataArchive = TwinRes_MbArchiveCreate();
    dataArchive.header = headerArchive;
    TwinStudio_StringView mainArchivePath = TwinStudio_CopyFromCStringArena(&archiveArena, openedFile);
    mainArchivePath.dynString[mainArchivePath.length - 1] = 'b';
    TwinStudio_BinarySerializer* mainArchiveDeserializer = TwinStudio_BinReadFromFile(mainArchivePath, true);
    TwinRes_MbArchiveDeserialize(&dataArchive, mainArchiveDeserializer, &archiveArena, TwinStudio_BinGetStreamLength(mainArchiveDeserializer), NULL);
    char* savePath = tinyfd_selectFolderDialog("Select tracks save folder", NULL);
    TwinStudio_StringView savePathString = TwinStudio_CopyFromCStringArena(&archiveArena, savePath);
    for (uint32_t i = 0; i < headerArchive.recordsAmount; ++i)
    {
        TwinRes_MbRecord* record = dataArchive.items + i;
        if (record->header.type == TwinRes_MRT_Null)
        {
            continue;
        }

        if (record->trackData.loopPosition > 0)
        {
            fprintf(stderr, "Track %d has a loop point at sample %d", i + 1, record->trackData.loopPosition);
        }

        char trackSavePathBuffer[1024];
        snprintf(trackSavePathBuffer, 1024, TS_VIEW_FORMAT"/%d_%s.wav", TS_VIEW_ARG(savePathString), i + 1, "track");
        TwinStudio_WaveSaveToFileC(trackSavePathBuffer, record->trackData);
    }

    arrfree(headerArchive.records);
    arrfree(dataArchive.items);
    TwinStudio_ArenaFree(&archiveArena);
    TwinStudio_BinSerializerFree(deserializer);
    TwinStudio_BinSerializerFree(mainArchiveDeserializer);
}


void UnpackBandicootArchives()
{
    const char* filterPatterns[1];
    filterPatterns[0] = "*.BH";
    char* openedFile = tinyfd_openFileDialog("Select BH archive", NULL, 1, filterPatterns, "*.BH Bandicoot Header", 0);
    if (openedFile == NULL)
    {
        fprintf(stderr, "No file opened!\n");
        return;
    }

    fprintf(stderr, "Opened %s\n", openedFile);
    TwinRes_BhArchive headerArchive = TwinRes_BhArchiveCreate();
    TwinStudio_Arena archiveArena = TwinStudio_CreateArena(1024UL * 1024UL * 200UL);
    TwinStudio_StringView filePath = TwinStudio_CopyFromCStringArena(&archiveArena, openedFile);
    TwinStudio_BinarySerializer* deserializer = TwinStudio_BinReadFromFile(filePath, false);
    TwinRes_BhArchiveBinDeserialize(&headerArchive, deserializer, &archiveArena, TwinStudio_BinGetStreamLength(deserializer), NULL);
    uint32_t recordsAmt = arrlen(headerArchive.records);
    for (uint32_t i = 0; i < recordsAmt; ++i)
    {
        fprintf(stderr, "%d. File "TS_VIEW_FORMAT". Size: %d\n", i + 1, TS_VIEW_ARG(headerArchive.records[i].path), headerArchive.records[i].length);
    }

    TwinRes_BdArchive dataArchive = TwinRes_BdArchiveCreate();
    dataArchive.header = headerArchive;
    TwinStudio_StringView mainArchivePath = TwinStudio_CopyFromCStringArena(&archiveArena, openedFile);
    mainArchivePath.dynString[mainArchivePath.length - 1] = 'D';
    TwinStudio_BinarySerializer* mainArchiveDeserializer = TwinStudio_BinReadFromFile(mainArchivePath, true);
    char* savePath = tinyfd_selectFolderDialog("Select files save folder", NULL);
    TwinStudio_StringView savePathString = TwinStudio_CopyFromCStringArena(&archiveArena, savePath);
    for (uint32_t i = 0; i < recordsAmt; ++i)
    {
        TwinStudio_Arena itemArena = TwinStudio_CreateArena(1024 * 1024 * 30);
        TwinRes_BdRecord record = TwinRes_BdArchiveIterateItem(&dataArchive, mainArchiveDeserializer, &itemArena, headerArchive.records[i].length, NULL);

        TwinStudio_StringReplace(&record.header.path, NULL, "\\", "/");
        char fileSavePathBuffer[1024];
        snprintf(fileSavePathBuffer, 1024, TS_VIEW_FORMAT"/"TS_VIEW_FORMAT, TS_VIEW_ARG(savePathString), TS_VIEW_ARG(record.header.path));

        TwinStudio_BinarySerializer* fileSaver = TwinStudio_BinSerializerAllocate(record.data, TwinStudio_BinarySerializerModeRead, record.header.length, false);
        TwinStudio_BinReadVoid(fileSaver, record.header.length);
        MakeDirectory(GetDirectoryPath(fileSavePathBuffer));
        TwinStudio_BinWriteToFileC(fileSavePathBuffer, fileSaver);
        TwinStudio_BinSerializerFree(fileSaver);
        TwinStudio_ArenaFree(&itemArena);
    }

    arrfree(headerArchive.records);
    TwinStudio_ArenaFree(&archiveArena);
    TwinStudio_BinSerializerFree(deserializer);
    TwinStudio_BinSerializerFree(mainArchiveDeserializer);
}


void HandleButtonClick(Clay_ElementId element, Clay_PointerData pointerData, void* data)
{
    const char* filterPatterns[1];
    filterPatterns[0] = "*.rm2";
    char* openedFile = tinyfd_openFileDialog("Select rm2 chunk", NULL, 1, filterPatterns, "*.rm2 Resource Manager 2", 0);
    if (openedFile == NULL)
    {
        fprintf(stderr, "No file opened!\n");
        return;
    }

    fprintf(stderr, "Opened %s\n", openedFile);

    TwinStudio_Arena chunkArena = TwinStudio_CreateArena(1024UL * 1024UL * 30UL);

    TwinStudio_StringView resourcePath = TwinStudio_CopyFromCStringArena(&chunkArena, openedFile);
    TwinStudio_StringView sceneryPath = TwinStudio_CopyFromCStringArena(&chunkArena, openedFile);
    sceneryPath.dynString[sceneryPath.length - 3] = 's';

    TwinRes_Chunk chunk = TwinRes_ChunkCreate();
    TwinStudio_BinarySerializer* resourceDeserializer = TwinStudio_BinReadFromFile(resourcePath, true);
    TwinStudio_BinarySerializer* sceneryDeserializer = TwinStudio_BinReadFromFile(sceneryPath, true);
    TwinRes_ChunkBinDeserialize(&chunk, &chunkArena, resourceDeserializer, sceneryDeserializer);

    TwinStudio_Arena chunkWriteArena = TwinStudio_CreateArena(1024UL * 1024UL * 30UL);
    char writePath[1024];
    snprintf(writePath, 1024, "%.*s_copy.rm2", (int32_t)strlen(openedFile) - 4, openedFile);
    TwinStudio_StringView resourceWritePath = TwinStudio_CopyFromCStringArena(&chunkWriteArena, writePath);
    TwinStudio_BinarySerializer* resourceSerializer = TwinStudio_BinWriteToFileStream(resourceWritePath);
    TwinStudio_StringView sceneryWritePath = TwinStudio_CopyFromCStringArena(&chunkWriteArena, writePath);
    sceneryWritePath.dynString[sceneryWritePath.length - 3] = 's';
    TwinStudio_BinarySerializer* scenerySerializer  = TwinStudio_BinWriteToFileStream(sceneryWritePath);
    TwinRes_ChunkBinSerialize(&chunk, &chunkWriteArena, resourceSerializer, scenerySerializer);

    fprintf(stderr, "Read chunk. Test value from collision %d\n", chunk.chunkResources.collision.unkInt);
    TwinStudio_BinSerializerFree(resourceDeserializer);
    TwinStudio_BinSerializerFree(sceneryDeserializer);
    TwinStudio_BinSerializerFree(resourceSerializer);
    TwinStudio_BinSerializerFree(scenerySerializer);
    TwinStudio_ArenaFree(&chunkArena);
    TwinStudio_ArenaFree(&chunkWriteArena);
}


int main(int argc, char** argv)
{
    rpmalloc_initialize(NULL);

    Clay_Raylib_Initialize(1280, 720, "Twin Studio", FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_HIGHDPI | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);

    uint64_t clayRequiredMemory = Clay_MinMemorySize();
    Clay_Arena clayMemory = Clay_CreateArenaWithCapacityAndMemory(clayRequiredMemory, malloc(clayRequiredMemory));
    Clay_Initialize(clayMemory, (Clay_Dimensions) { .width = GetScreenWidth(), .height = GetScreenHeight() }, (Clay_ErrorHandler) { HandleClayErrors });

    TwinRes_ChunkSerializationInit();
    TwinStudio_TextureSerializationInit();
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