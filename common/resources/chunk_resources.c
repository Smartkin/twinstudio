#include <assert.h>
#include <stb_ds.h>
#include <stdio.h>
#include "chunk_resources.h"
#include "resources/resources.h"


void TwinStudio_AddChunkResource(TwinStudio_ChunkResourceManager* chunkRes, TwinStudio_ResourceType type, TwinStudio_SoundResourceType soundType, uint32_t twinId, void* data)
{
    TwinStudio_ChunkResourceKey key = { .key = 0, .soundType = soundType, .twinId = twinId, .resType = type };
    TwinStudio_ChunkResource resource = { .key = key, .data = data };
    TwinStudio_ChunkResourceEntry entry = { .keyData = key, .value = resource };
    hmputs(chunkRes->chunkResources, entry);
}


TwinStudio_ChunkResource TwinStudio_GetChunkResource(TwinStudio_ChunkResourceManager* chunkRes, TwinStudio_ResourceType type, TwinStudio_SoundResourceType soundType, uint32_t twinId)
{
    TwinStudio_ChunkResourceKey key = { .soundType = soundType, .twinId = twinId, .resType = type };
    size_t idx = hmgeti(chunkRes->chunkResources, key.key);
    if (idx < 0)
    {
        return (TwinStudio_ChunkResource) { 0 };
    }
    return hmget(chunkRes->chunkResources, key.key);
}


TwinStudio_ChunkResource* TwinStudio_GetChunkResourcesByType(TwinStudio_ChunkResourceManager* chunkRes, TwinStudio_ResourceType type, size_t* amount)
{
    size_t n = 0;
    TwinStudio_ChunkResource* result = NULL;

    for (size_t i = 0; i < hmlenu(chunkRes->chunkResources); ++i)
    {
        TwinStudio_ChunkResource resource = chunkRes->chunkResources[i].value;
        if (resource.key.resType == type)
        {
            arrput(result, resource);
            n++;
        }
    }

    if (amount)
    {
        *amount = n;
    }
    return result;
}

static const char* resTypeStrings[] = { "wut?",
    "Texture",
    "Mesh",
    "Lod",
    "RigidModel",
    "Sound",
    "Material",
    "Model",
    "Skin",
    "BlendSkin",
    "Chunk",
    "Animation",
    "Object",
    "Behavior",
    "SequenceBehavior",
    "Instance",
    "Path",
    "Camera",
    "Trigger",
    "Skydome",
    "CollisionSurface",
    "Position",
    "AI_Position",
    "AI_Path",
    "Body",
    "ParticleSystem",
    "SequenceBehaviorAction",
    "DynamicScenery",
    "Scenery",
    "Collision",
    "Localized",
 };

static int32_t ctz32(uint32_t x)
{
    int32_t n = 1;
    assert(x != 0);
    while (!(x & 1u))
    {
        x >>= 1;
        ++n;
    }

    return n;
}

static const char* ResTypeToString(TwinStudio_ResourceType resType)
{
    return resTypeStrings[ctz32(resType)];
}

void TwinStudio_DumpChunkResources(TwinStudio_ChunkResourceManager* chunkRes)
{
    fprintf(stderr, "------- Chunk Resources -------\n");
    for (size_t i = 0; i < hmlenu(chunkRes->chunkResources); ++i)
    {
        TwinStudio_ChunkResource resource = chunkRes->chunkResources[i].value;
        fprintf(stderr, "%zu. Type %s Id %u\n", i + 1, ResTypeToString(resource.key.resType), resource.key.twinId);
    }
}