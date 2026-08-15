#ifndef TS_CHUNK_RESOURCES_H
#define TS_CHUNK_RESOURCES_H

#include <stdint.h>
#include "resources.h"

typedef union TwinStudio_ChunkResourceKey {
    uint64_t key;
    struct {
        uint32_t twinId     : 32;
        uint32_t resType    : 29;
        uint32_t  soundType : 3;
    };
} TwinStudio_ChunkResourceKey;

typedef struct TwinStudio_ChunkResource {
    TwinStudio_ChunkResourceKey key;
    void* data;
} TwinStudio_ChunkResource;

typedef struct TwinStudio_ChunkResourceEntry {
    union {
        uint64_t key;
        TwinStudio_ChunkResourceKey keyData;
    };
    TwinStudio_ChunkResource value;
} TwinStudio_ChunkResourceEntry;

typedef struct TwinStudio_ChunkResourceManager {
    TwinStudio_ChunkResourceEntry* chunkResources;
} TwinStudio_ChunkResourceManager;


void TwinStudio_AddChunkResource(TwinStudio_ChunkResourceManager* chunkRes, TwinStudio_ResourceType type, TwinStudio_SoundResourceType soundType, uint32_t twinId, void* data);
TwinStudio_ChunkResource TwinStudio_GetChunkResource(TwinStudio_ChunkResourceManager* chunkRes, TwinStudio_ResourceType type, TwinStudio_SoundResourceType soundType, uint32_t twinId);
TwinStudio_ChunkResource* TwinStudio_GetChunkResourcesByType(TwinStudio_ChunkResourceManager* chunkRes, TwinStudio_ResourceType type, size_t* amount);
void TwinStudio_DumpChunkResources(TwinStudio_ChunkResourceManager* chunkRes);

#endif // TS_CHUNK_RESOURCES_H