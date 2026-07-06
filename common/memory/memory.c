#include "memory.h"

#include <assert.h>
#include <rpmalloc.h>
#include <string.h>


TwinStudio_Arena TwinStudio_CreateArena(size_t size)
{
    void* mem = rpmalloc(size);
    memset(mem, 0, size);
    return (TwinStudio_Arena) {
        .startMemory = mem,
        .currentMemory = mem,
        .allocedMemorySize = 0,
        .size = size
    };
}


void* TwinStudio_ArenaAlloc(TwinStudio_Arena* arena, size_t size)
{
    assert(arena->allocedMemorySize + size <= arena->size);
    void* alloc = arena->currentMemory;
    // This used to be just += but make stupid Windows compiler happy :)
    arena->currentMemory = ((char*)arena->currentMemory) + size;
    arena->allocedMemorySize += size;
    return alloc;
}


void TwinStudio_ArenaFree(TwinStudio_Arena* arena)
{
    rpfree(arena->startMemory);
}