#include <assert.h>
#include <cJSON.h>
#include <cJSON_Utils.h>
#include <rpmalloc.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <raylib.h>

#include "json_serializer.h"
#include "string_view/string_view.h"

bool gIsJsonInitialized = false;
static char smallJson[1024 * 16]; // small json files optimization to not allocate string on the heap


void TwinStudio_JsonSerializerInit()
{
    cJSON_Hooks hooks = { .free_fn = rpfree, .malloc_fn = rpmalloc };
    cJSON_InitHooks(&hooks);
    gIsJsonInitialized = true;
}


void TwinStudio_JsonWriteToFile(TwinStudio_StringView path, const cJSON* json)
{
    assert(gIsJsonInitialized);
    
    FILE* f = fopen(TwinStudio_GetCString(&path), "w");
    char* jsonStr = cJSON_Print(json);
    fwrite(jsonStr, strlen(jsonStr), 1, f);
    fclose(f);
    cJSON_free(jsonStr);
}


cJSON* TwinStudio_JsonReadFromFile(TwinStudio_StringView path)
{
    assert(gIsJsonInitialized);

    const char* cPath = TwinStudio_GetCString(&path);
    const uint64_t fileSize = GetFileLength(cPath);
    FILE* f = fopen(cPath, "r");
    if (fileSize <= 1024 * 16)
    {
        fread(smallJson, fileSize, 1, f);
        return cJSON_Parse(smallJson);
    }

    char* dynAllocated = rpmalloc(fileSize);
    fread(dynAllocated, fileSize, 1, f);
    cJSON* result = cJSON_Parse(dynAllocated);
    rpfree(dynAllocated);
    return result;
}
