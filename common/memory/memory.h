#ifndef TS_MEMORY_H
#define TS_MEMORY_H

#include <stddef.h>


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