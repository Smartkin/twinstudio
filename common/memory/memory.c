#include "memory.h"

#include <assert.h>
#include <rpmalloc.h>
#include <stdio.h>
#include <string.h>
#include <stb_ds.h>
#include <stdint.h>

TwinStudio_Arena TwinStudio_CreateArena(size_t size)
{
    void* mem = TWIN_MALLOC(size);
    memset(mem, 0, size);
    return (TwinStudio_Arena) {
        .startMemory = mem,
        .currentMemory = mem,
        .allocedMemorySize = 0,
        .size = size,
        .currentExtraArena = -1,
    };
}


void* TwinStudio_ArenaAlloc(TwinStudio_Arena* arena, size_t size)
{
    if (arena->allocedMemorySize + size > arena->size)
    {
        if (arena->currentExtraArena < 0 || arena->extraArenas[arena->currentExtraArena].allocedMemorySize + size > arena->size)
        {
            fprintf(stderr, "WARNING: Arena allocated memory was exceeded! Extra space was allocated! This could result in performance penalty!\n");
            arrput(arena->extraArenas, TwinStudio_CreateArena(arena->size));
            arena->currentExtraArena = arrlen(arena->extraArenas) - 1;
        }
        arena = arena->extraArenas + arena->currentExtraArena;
    }

    void* alloc = arena->currentMemory;
    // This used to be just += but make stupid Windows compiler happy :)
    arena->currentMemory = ((char*)arena->currentMemory) + size;
    arena->allocedMemorySize += size;
    return alloc;
}


void TwinStudio_ArenaFree(TwinStudio_Arena* arena)
{
    if (arena->currentExtraArena >= 0)
    {
        for (uint32_t i = 0; i < arrlen(arena->extraArenas); ++i)
        {
            TWIN_FREE(arena->extraArenas[i].startMemory);
        }

        arrfree(arena->extraArenas);
    }

    TWIN_FREE(arena->startMemory);
}