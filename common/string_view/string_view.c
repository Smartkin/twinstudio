#include "string_view.h"
#include "memory/memory.h"
#include <assert.h>
#include <rpmalloc.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static TwinStudio_StringView* AllocateNewString(char* dynStr, uint32_t size)
{
    TwinStudio_StringView* resultStr = TWIN_MALLOC(sizeof(TwinStudio_StringView));
    resultStr->dynString = dynStr;
    resultStr->length = size;
    resultStr->allocatedLength = size + 1;
    resultStr->isDynamicallyAllocated = true;
    resultStr->isSelfDynamicallyAllocated = true;
    resultStr->dynString[resultStr->length] = '\0';

    return resultStr;
}


TwinStudio_StringView* TwinStudio_ConvertStringToString(void* data)
{
    TwinStudio_StringView* originalStr = data;
    char* strCopy = TWIN_MALLOC(originalStr->length + 1);
    memcpy(strCopy, originalStr->string, originalStr->length);

    return AllocateNewString(strCopy, originalStr->length);
}


TwinStudio_StringView* TwinStudio_ConvertIntToString(void* data)
{
    const int* integer = data;
    char intStr[32];
    const int charsTotal = snprintf(intStr, sizeof(intStr), "%d", *integer);
    char* dynStr = TWIN_MALLOC(charsTotal + 1);
    memcpy(dynStr, intStr, charsTotal);

    return AllocateNewString(dynStr, charsTotal);
}


TwinStudio_StringView* TwinStudio_ConvertFloatToString(void* data)
{
    const float* floating = data;
    char floatStr[256];
    const int charsTotal = snprintf(floatStr, sizeof(floatStr), "%f", *floating);
    char* dynStr = TWIN_MALLOC(charsTotal + 1);
    memcpy(dynStr, floatStr, charsTotal);

    return AllocateNewString(dynStr, charsTotal);
}


TwinStudio_StringView  TwinStudio_CopyFromCStringArena(TwinStudio_Arena* arena, const char* cStr)
{
    const uint32_t stringSize = strlen(cStr);
    char* dynStr = TwinStudio_ArenaAlloc(arena, stringSize);
    memcpy(dynStr, cStr, stringSize);

    return (TwinStudio_StringView) { .dynString = dynStr, .isDynamicallyAllocated = true, .length = stringSize, .allocatedLength = stringSize };
}


TwinStudio_StringView TwinStudio_CopyFromCString(const char* cStr)
{
    const uint32_t stringSize = strlen(cStr);
    char* dynStr = TWIN_MALLOC(stringSize);
    memcpy(dynStr, cStr, stringSize);

    return (TwinStudio_StringView) { .dynString = dynStr, .isDynamicallyAllocated = true, .length = stringSize, .allocatedLength = stringSize };
}


void TwinStudio_ConvertBackStringToInt(void* dest, TwinStudio_StringView* string)
{
    int* destInt = dest;
    *destInt = strtoimax(string->string, NULL, 0);
}


void TwinStudio_ConvertBackStringToFloat(void* dest, TwinStudio_StringView* string)
{
    float* destFloat = dest;
    *destFloat = strtof(string->string, NULL);
}


const char* TwinStudio_GetCString(TwinStudio_StringView* string)
{
    if (string->isDynamicallyAllocated)
    {
        string->dynString[string->length] = '\0';
    }
    return string->string;
}


bool TwinStudio_StringContains(const TwinStudio_StringView* string, char c)
{
    for (uint32_t i = 0; i < string->length; ++i)
    {
        if (string->string[i] == c)
        {
            return true;
        }
    }

    return false;
}


char* TwinStudio_GetCStringDynamic(TwinStudio_StringView* string)
{
    char* cString = TWIN_MALLOC(string->length + 1);
    memcpy(cString, string->string, string->length);
    if (string->string[string->length - 1] != '\0')
    {
        cString[string->length] = '\0';
    }

    return cString;
}


void TwinStudio_StringReplace(TwinStudio_StringView *string, TwinStudio_Arena* arena, const char *search, const char *replaceWith)
{
    assert(string->isDynamicallyAllocated);

    const int32_t searchLen = strlen(search);
    const int32_t replaceWithLen = strlen(replaceWith);
    const int32_t lenDiff = searchLen - replaceWithLen;
    int32_t replacementCount = 0;
    // In place replacement. Really fast, really nice
    if (lenDiff == 0)
    {
        char* replacement = NULL;
        char* currentSearch = string->dynString;
        
        while ((replacement = strstr(currentSearch, search)) != NULL)
        {
            memcpy(replacement, replaceWith, replaceWithLen);
            currentSearch = replacement;
        }
    }
    else
    {
        TwinStudio_StringView stringCopy = TwinStudio_CopyFromCStringArena(arena, string->string);
        char* replacement = NULL;
        char* currentSearch = stringCopy.dynString;
        
        int32_t moveTotal = 0;
        while ((replacement = strstr(currentSearch, search)) != NULL)
        {
            const int32_t movedAmount = replacement - currentSearch;
            if (lenDiff > 0)
            {
                moveTotal += movedAmount + searchLen;
                memcpy(replacement, replaceWith, replaceWithLen);
                memmove(replacement + replaceWithLen, replacement + replaceWithLen + lenDiff, stringCopy.length - moveTotal);
            }
            else
            {
                moveTotal += movedAmount + replaceWithLen;
                memmove(replacement + replaceWithLen, replacement + searchLen, stringCopy.length - moveTotal);
                memcpy(replacement, replaceWith, replaceWithLen);
            }
            currentSearch = replacement;
            replacementCount++;
        }

        string->dynString = stringCopy.dynString;
        string->length += (-lenDiff) * replacementCount;
    }
}

void TwinStudio_FreeString(TwinStudio_StringView* string)
{
    if (!string->isDynamicallyAllocated) {
        return;
    }

    TWIN_FREE(string->dynString);
    string->dynString = NULL;
    string->string = NULL;
    string->length = 0;
    
    if (string->isSelfDynamicallyAllocated)
    {
        TWIN_FREE(string);
    }
}