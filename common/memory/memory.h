#ifndef TS_MEMORY_H
#define TS_MEMORY_H

#include <stddef.h>
#include <rpmalloc.h>
#include <stdlib.h>

#ifdef TWIN_ASAN
#define TWIN_MALLOC malloc
#define TWIN_REALLOC realloc
#define TWIN_FREE free
#define rpmalloc_initialize(...)
#define rpmalloc_finalize(...)
#else
#define TWIN_MALLOC rpmalloc
#define TWIN_REALLOC rprealloc
#define TWIN_FREE rpfree
#endif

typedef struct TwinStudio_Arena {
    void* currentMemory;
    void* startMemory;
    size_t size;
    size_t allocedMemorySize;
} TwinStudio_Arena;


TwinStudio_Arena TwinStudio_CreateArena(size_t size);
void* TwinStudio_ArenaAlloc(TwinStudio_Arena* arena, size_t size);
void  TwinStudio_ArenaFree(TwinStudio_Arena* arena);


#endif // TS_MEMORY_H