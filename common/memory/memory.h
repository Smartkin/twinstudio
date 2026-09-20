#ifndef TS_MEMORY_H
#define TS_MEMORY_H

#include <stddef.h>
#include <rpmalloc.h>
#include <stdlib.h>
#include <stdint.h>

#define TWIN_MALLOC rpmalloc
#define TWIN_REALLOC rprealloc
#define TWIN_FREE rpfree

typedef struct TwinStudio_Arena {
    void* currentMemory;
    void* startMemory;
    size_t size;
    size_t allocedMemorySize;
    int32_t currentExtraArena;
    struct TwinStudio_Arena* extraArenas;
} TwinStudio_Arena;


TwinStudio_Arena TwinStudio_CreateArena(size_t size);
TwinStudio_Arena TwinStudio_CreateArenaFromMem(void* mem, size_t size);
void* TwinStudio_ArenaAlloc(TwinStudio_Arena* arena, size_t size);
void  TwinStudio_ArenaFree(TwinStudio_Arena* arena);


#endif // TS_MEMORY_H