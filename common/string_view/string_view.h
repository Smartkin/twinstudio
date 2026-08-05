#ifndef TS_COMMON_STRING_VIEW_H
#define TS_COMMON_STRING_VIEW_H

#include "memory/memory.h"
#include <clay.h>
#include <stdint.h>

typedef struct TwinStudio_StringView {
    union {
        const char* string;
        char* dynString;
    };

    uint32_t allocatedLength;
    uint32_t length;
    bool isDynamicallyAllocated;
    bool isSelfDynamicallyAllocated;
} TwinStudio_StringView;


#define TS_STRING_VIEW(str) (TwinStudio_StringView) { .string = str, .length = sizeof(str) - 1 }
#define TS_VIEW_FORMAT "%.*s"
#define TS_VIEW_ARG(str) (str).length, (str).string

#define TS_STRING_TO_CLAY(str_view) (Clay_String) { .isStaticallyAllocated = false, .length = (str_view).length, .chars = (str_view).string }

// All resulting strings from conversion functions below are dynamically allocated!
// Don't forget to call TwinStudio_FreeString on them after not needing them!
TwinStudio_StringView* TwinStudio_ConvertStringToString(void* data);
TwinStudio_StringView* TwinStudio_ConvertIntToString(void* data);
TwinStudio_StringView* TwinStudio_ConvertFloatToString(void* data);
TwinStudio_StringView  TwinStudio_CopyFromCString(const char* cStr);
TwinStudio_StringView  TwinStudio_CopyFromCStringArena(TwinStudio_Arena* arena, const char* cStr);

void TwinStudio_ConvertBackStringToInt(void* dest, TwinStudio_StringView* string);
void TwinStudio_ConvertBackStringToFloat(void* desc, TwinStudio_StringView* string);

const char* TwinStudio_GetCString(TwinStudio_StringView* string);
char* TwinStudio_GetCStringDynamic(TwinStudio_StringView* string); // This allocates a C string dynamically! Don't forget to free it

bool TwinStudio_StringContains(const TwinStudio_StringView* string, char c);

// arena can be null if search and replace strings are of equal length
void TwinStudio_StringReplace(TwinStudio_StringView* string, TwinStudio_Arena* arena, const char* search, const char* replaceWith);

void TwinStudio_FreeString(TwinStudio_StringView* string);

#endif // TS_COMMON_STRING_VIEW_H