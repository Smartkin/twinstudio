#include <cJSON.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "chunk_serializer.h"
#include "memory/memory.h"
#include "ps2/audio_serializers.h"
#include "ps2/auto_struct_base.h"
#include "ps2/retail/auto_struct_chunk.h"
#include "ps2/retail/auto_struct_mb_archive.h"
#include "ps2/retail/auto_struct_mh_archive.h"
#include "ps2/retail/rm2/code/auto_struct_sound.h"
#include "ps2/retail/sm2/auto_struct_scenery.h"
#include "serialization/binary_serializer.h"


static TwinRes_SectionSerdeInfo rm2Serdes[12];
static TwinRes_SectionSerdeInfo defaultSerdes[12];
static TwinRes_SectionSerdeInfo sm2Serdes[7];
static TwinRes_SectionSerdeInfo layoutSerdes[9];
static TwinRes_SectionSerdeInfo graphicsSerdes[9];
static TwinRes_SectionSerdeInfo codeSerdes[13];

// RM2 and SM2
TS_WRAP_SERDE_IN_VOID_ALL(TwinRes_GraphicsSection);

// SM2 (Scenery Manager 2)
TS_WRAP_SERDE_IN_VOID_ALL(TwinRes_Scenery);
TS_WRAP_SERDE_IN_VOID_ALL(TwinRes_Item);
TS_WRAP_SERDE_IN_VOID_ALL(TwinRes_DynamicScenery);
TS_WRAP_SERDE_IN_VOID_ALL(TwinRes_ChunkLinks);

// RM2 (Resource Manager 2)
TS_WRAP_SERDE_IN_VOID_ALL(TwinRes_LayoutSection);
TS_WRAP_SERDE_IN_VOID_ALL(TwinRes_Particles);
TS_WRAP_SERDE_IN_VOID_ALL(TwinRes_DefaultParticles); // For default.rm2
TS_WRAP_SERDE_IN_VOID_ALL(TwinRes_StaticCollision);
TS_WRAP_SERDE_IN_VOID_ALL(TwinRes_CodeSection);

// Graphics section
TS_WRAP_SERDE_IN_VOID_ALL(TwinRes_TexturesSection);
TS_WRAP_SERDE_IN_VOID_ALL(TwinRes_MaterialsSection);
TS_WRAP_SERDE_IN_VOID_ALL(TwinRes_ModelsSection);
TS_WRAP_SERDE_IN_VOID_ALL(TwinRes_RigidModelsSection);
TS_WRAP_SERDE_IN_VOID_ALL(TwinRes_SkinsSection);
TS_WRAP_SERDE_IN_VOID_ALL(TwinRes_BlendSkinsSection);
TS_WRAP_SERDE_IN_VOID_ALL(TwinRes_MeshesSection);
TS_WRAP_SERDE_IN_VOID_ALL(TwinRes_LodsSection);
TS_WRAP_SERDE_IN_VOID_ALL(TwinRes_SkydomesSection);

// Code section
TS_WRAP_SERDE_IN_VOID_ALL(TwinRes_ObjectsSection);
TS_WRAP_SERDE_IN_VOID_ALL(TwinRes_AgentLabBehaviorsSection);
TS_WRAP_SERDE_IN_VOID_ALL(TwinRes_AnimationsSection);
TS_WRAP_SERDE_IN_VOID_ALL(TwinRes_BodiesSection);
TS_WRAP_SERDE_IN_VOID_ALL(TwinRes_AgentLabSequencesSection);

// Layout section
TS_WRAP_SERDE_IN_VOID_ALL(TwinRes_InstanceTemplatesSection);
TS_WRAP_SERDE_IN_VOID_ALL(TwinRes_AiPositionsSection);
TS_WRAP_SERDE_IN_VOID_ALL(TwinRes_AiPathsSection);
TS_WRAP_SERDE_IN_VOID_ALL(TwinRes_PositionsSection);
TS_WRAP_SERDE_IN_VOID_ALL(TwinRes_PathsSection);
TS_WRAP_SERDE_IN_VOID_ALL(TwinRes_CollisionSurfacesSection);
TS_WRAP_SERDE_IN_VOID_ALL(TwinRes_InstancesSection);
TS_WRAP_SERDE_IN_VOID_ALL(TwinRes_TriggersSection);
TS_WRAP_SERDE_IN_VOID_ALL(TwinRes_CamerasSection);

#define SECTION_SERDE(SECTION_TYPE, ITEM_TYPE, ITEM_MEMBER) (TwinRes_SectionSerdeInfo) { .targetOffset = offsetof(SECTION_TYPE, ITEM_MEMBER), .deserialFunc = TS_GET_DESERIAL_WRAP(ITEM_TYPE), .serialFunc = TS_GET_SERIAL_WRAP(ITEM_TYPE) }

static uint16_t GetSoundSampleRate(uint16_t sampleFactor)
{
    switch (sampleFactor) {
        case 2:
            return 8000;
        case 3:
            return 10000;
        case 4:
            return 11025;
        case 5:
            return 16000;
        case 6:
            return 18000;
        case 7:
            return 22050;
        case 0xA:
            return 32000;
        case 0xE:
            return 44100;
        case 0x10:
            return 48000;
    };

    return 48000; // Default to 48k
}

static uint16_t GetSoundSampleFactor(uint16_t sampleRate)
{
    switch (sampleRate) {
        case 8000:
            return 2;
        case 10000:
            return 3;
        case 11025:
            return 4;
        case 16000:
            return 5;
        case 18000:
            return 6;
        case 22050:
            return 7;
        case 32000:
            return 0xA;
        case 44100:
            return 0xE;
        case 48000:
            return 0x10;
    };

    return 0x10; // Default to 48k
}

static void TwinRes_SoundEffectsSerializerWrapper(void* source, TwinStudio_BinarySerializer* serializer, TwinStudio_Arena* arena, size_t size, void* userData)
{
    TwinRes_SectionHeader* header = source;
    TwinRes_SoundEffectsSection* sfxSection = source;
    const size_t baseOffset = TwinStudio_BinGetStreamPosition(serializer);
    header->itemsCount = arrlen(header->records);
    uint32_t offset = 0;

    TwinStudio_Arena tempAdpcmArena = TwinStudio_CreateArena(1024 * 1024 * 10);
    TwinStudio_Arena tempArena = TwinStudio_CreateArena(1024 * 1024 * 10);
    uint8_t* soundData = TwinStudio_ArenaAlloc(&tempArena, 1024 * 1024 * 5);
    uint8_t* stereoSoundData = TwinStudio_ArenaAlloc(&tempArena, 1024 * 1024 * 5);
    TwinStudio_BinarySerializer* stereoSoundDataSerial = TwinStudio_BinSerializerAllocate(stereoSoundData, TwinStudio_BinarySerializerModeWrite, 1024 * 1024 * 5, false);
    TwinStudio_BinarySerializer* soundDataSerial = TwinStudio_BinSerializerAllocate(soundData, TwinStudio_BinarySerializerModeWrite, 1024 * 1024 * 5, false);
    for (uint32_t i = 0; i < header->itemsCount; ++i)
    {
        TwinRes_Sound* sfx = sfxSection->soundEffects + i;
        sfx->adpcmData = TwinStudio_ArenaAlloc(&tempAdpcmArena, sfx->trackData.dataSize);
        TwinStudio_BinarySerializer* adpcmSerial = TwinStudio_BinSerializerAllocate(sfx->adpcmData, TwinStudio_BinarySerializerModeWrite, sfx->trackData.dataSize, false);
        if (sfx->isMono)
        {
            TwinRes_MbRecord fakeRecord = {
                .header = { .type = TwinRes_MRT_Mono, .interleave = 8192 }
            };
            TwinStudio_WaveBinSerialize(&sfx->trackData, adpcmSerial, arena, sfx->soundSize, &fakeRecord);
        }
        else
        {
            TwinRes_MbRecord fakeRecord = {
                .header = { .type = TwinRes_MRT_Stereo, .interleave = sfx->trackData.dataSize / 4 }
            };
            TwinStudio_WaveBinSerialize(&sfx->trackData, adpcmSerial, arena, sfx->soundSize, &fakeRecord);
        }

        sfx->soundSize = TwinStudio_BinGetStreamPosition(adpcmSerial);
        sfx->frequencyId = GetSoundSampleFactor(sfx->trackData.samplerate);
        uint32_t writeLength = sfx->soundSize;
        sfx->isMono = sfx->trackData.channels == 1;
        // Write sound data
        if (!sfx->isMono)
        {
            writeLength /= 2;
        }

        TwinStudio_BinWriteBlob(soundDataSerial, sfx->adpcmData, writeLength);
        sfx->offset = offset;
        offset += writeLength;

        TwinStudio_BinSerializerFree(adpcmSerial);
    }

    for (uint32_t i = 0; i < header->itemsCount; ++i)
    {
        TwinRes_Sound* sfx = sfxSection->soundEffects + i;
        if (sfx->isMono)
        {
            continue;
        }

        const uint32_t halfSize = sfx->soundSize / 2;
        TwinStudio_BinWriteBlob(soundDataSerial, sfx->adpcmData + halfSize, halfSize);
    }


    sfxSection->soundData = soundData;
    TwinRes_SoundEffectsSectionBinSerialize(source, serializer, arena, size, userData);
    TwinStudio_BinWriteBlob(serializer, soundData, TwinStudio_BinGetStreamPosition(soundDataSerial));

    TwinStudio_ArenaFree(&tempArena);
    TwinStudio_ArenaFree(&tempAdpcmArena);
    TwinStudio_BinSerializerFree(soundDataSerial);
    TwinStudio_BinSerializerFree(stereoSoundDataSerial);
}

static void TwinRes_SoundEffectsDeserializerWrapper(TwinStudio_DeserializationContext* ctx, void* target, TwinStudio_BinarySerializer* deserial, TwinStudio_Arena* arena, size_t size, void* userData)
{
    TwinRes_SoundEffectsSection* sfxSection = target;
    const size_t baseOffset = TwinStudio_BinGetStreamPosition(deserial);
    TwinRes_SoundEffectsSectionBinDeserialize(ctx, target, deserial, arena, size, userData);
    const uint32_t extraLength = size - (TwinStudio_BinGetStreamPosition(deserial) - baseOffset);
    sfxSection->soundData = TwinStudio_BinReadBlob(deserial, arena, extraLength);

    TwinStudio_Arena soundDataArena = TwinStudio_CreateArena(extraLength);
    uint8_t* extraDataUnwrapped = TwinStudio_ArenaAlloc(&soundDataArena, extraLength);
    uint32_t offset = 0;
    uint32_t unwrappedOffset = 0;
    for (uint32_t i = 0; i < sfxSection->header.itemsCount; ++i)
    {
        TwinRes_Sound* sfx = sfxSection->soundEffects + i;
        uint32_t copyLength = sfx->soundSize;
        if (!sfx->isMono)
        {
            copyLength /= 2;
        }

        memcpy(extraDataUnwrapped + unwrappedOffset, ((uint8_t*)sfxSection->soundData) + offset, copyLength);
        offset += copyLength;
        unwrappedOffset += sfx->soundSize;
    }

    unwrappedOffset = 0;
    for (uint32_t i = 0; i < sfxSection->header.itemsCount; ++i)
    {
        TwinRes_Sound* sfx = sfxSection->soundEffects + i;
        if (sfx->isMono)
        {
            unwrappedOffset += sfx->soundSize;
            continue;
        }

        uint32_t copyLength = sfx->soundSize / 2;
        memcpy(extraDataUnwrapped + unwrappedOffset + copyLength, ((uint8_t*)sfxSection->soundData) + offset, copyLength);
        offset += copyLength;
        unwrappedOffset += sfx->soundSize;
    }

    // Finally convert data after all the unswizzling of stereo + mono mixed sounds if that is ever the case
    TwinStudio_BinarySerializer* soundDataDeserial = TwinStudio_BinSerializerAllocate(extraDataUnwrapped, TwinStudio_BinarySerializerModeRead, extraLength, false);
    for (uint32_t i = 0; i < sfxSection->header.itemsCount; ++i)
    {
        TwinRes_Sound* sfx = sfxSection->soundEffects + i;

        TwinRes_MbRecord fakeRecord = { 
            .header = {
                .type = (sfx->isMono) ? TwinRes_MRT_Mono : TwinRes_MRT_Stereo,
                .interleave = sfx->soundSize / 2,
                .size = sfx->soundSize,
                .sampleRate = GetSoundSampleRate(sfx->frequencyId)
            },
            .trackSize = sfx->soundSize,
            .sampleRate = GetSoundSampleRate(sfx->frequencyId)
        };
        TwinStudio_WaveBinDeserialize(ctx, &sfx->trackData, soundDataDeserial, arena, sfx->soundSize, &fakeRecord);
    }

    TwinStudio_BinSerializerFree(soundDataDeserial);
    TwinStudio_ArenaFree(&soundDataArena);
}

void TwinRes_ChunkSerializationInit()
{
    graphicsSerdes[0] = SECTION_SERDE(TwinRes_GraphicsSection, TwinRes_TexturesSection, texturesSection);
    graphicsSerdes[1] = SECTION_SERDE(TwinRes_GraphicsSection, TwinRes_MaterialsSection, materialsSection);
    graphicsSerdes[2] = SECTION_SERDE(TwinRes_GraphicsSection, TwinRes_ModelsSection, modelsSection);
    graphicsSerdes[3] = SECTION_SERDE(TwinRes_GraphicsSection, TwinRes_RigidModelsSection, rigidModelsSection);
    graphicsSerdes[4] = SECTION_SERDE(TwinRes_GraphicsSection, TwinRes_SkinsSection, skinsSection);
    graphicsSerdes[5] = SECTION_SERDE(TwinRes_GraphicsSection, TwinRes_BlendSkinsSection, blendSkinsSection);
    graphicsSerdes[6] = SECTION_SERDE(TwinRes_GraphicsSection, TwinRes_MeshesSection, meshesSection);
    graphicsSerdes[7] = SECTION_SERDE(TwinRes_GraphicsSection, TwinRes_LodsSection, lodsSection);
    graphicsSerdes[8] = SECTION_SERDE(TwinRes_GraphicsSection, TwinRes_SkydomesSection, skydomesSection);

    sm2Serdes[0] = SECTION_SERDE(TwinRes_SceneryChunk, TwinRes_Scenery, scenery);
    sm2Serdes[1] = SECTION_SERDE(TwinRes_SceneryChunk, TwinRes_Item, unusedItem1);
    sm2Serdes[2] = SECTION_SERDE(TwinRes_SceneryChunk, TwinRes_Item, unusedItem2);
    sm2Serdes[3] = SECTION_SERDE(TwinRes_SceneryChunk, TwinRes_Item, unusedItem3);
    sm2Serdes[4] = SECTION_SERDE(TwinRes_SceneryChunk, TwinRes_DynamicScenery, dynamicScenery);
    sm2Serdes[5] = SECTION_SERDE(TwinRes_SceneryChunk, TwinRes_ChunkLinks, chunkLinks);
    sm2Serdes[6] = SECTION_SERDE(TwinRes_SceneryChunk, TwinRes_GraphicsSection, graphicsSection);

    rm2Serdes[0] = SECTION_SERDE(TwinRes_ResourceChunk, TwinRes_LayoutSection, layout1Section);
    rm2Serdes[1] = SECTION_SERDE(TwinRes_ResourceChunk, TwinRes_LayoutSection, layout2Section);
    rm2Serdes[2] = SECTION_SERDE(TwinRes_ResourceChunk, TwinRes_LayoutSection, layout3Section);
    rm2Serdes[3] = SECTION_SERDE(TwinRes_ResourceChunk, TwinRes_LayoutSection, layout4Section);
    rm2Serdes[4] = SECTION_SERDE(TwinRes_ResourceChunk, TwinRes_LayoutSection, layout5Section);
    rm2Serdes[5] = SECTION_SERDE(TwinRes_ResourceChunk, TwinRes_LayoutSection, layout6Section);
    rm2Serdes[6] = SECTION_SERDE(TwinRes_ResourceChunk, TwinRes_LayoutSection, layout7Section);
    rm2Serdes[7] = SECTION_SERDE(TwinRes_ResourceChunk, TwinRes_Item, layout8Section);
    rm2Serdes[8] = SECTION_SERDE(TwinRes_ResourceChunk, TwinRes_Particles, particles);
    rm2Serdes[9] = SECTION_SERDE(TwinRes_ResourceChunk, TwinRes_StaticCollision, collision);
    rm2Serdes[10] = SECTION_SERDE(TwinRes_ResourceChunk, TwinRes_CodeSection, codeSection);
    rm2Serdes[11] = SECTION_SERDE(TwinRes_ResourceChunk, TwinRes_GraphicsSection, graphicsSection);

    defaultSerdes[0] = SECTION_SERDE(TwinRes_DefaultResourceChunk, TwinRes_LayoutSection, layout1Section);
    defaultSerdes[1] = SECTION_SERDE(TwinRes_DefaultResourceChunk, TwinRes_Item, layout2Section);
    defaultSerdes[2] = SECTION_SERDE(TwinRes_DefaultResourceChunk, TwinRes_Item, layout3Section);
    defaultSerdes[3] = SECTION_SERDE(TwinRes_DefaultResourceChunk, TwinRes_Item, layout4Section);
    defaultSerdes[4] = SECTION_SERDE(TwinRes_DefaultResourceChunk, TwinRes_Item, layout5Section);
    defaultSerdes[5] = SECTION_SERDE(TwinRes_DefaultResourceChunk, TwinRes_Item, layout6Section);
    defaultSerdes[6] = SECTION_SERDE(TwinRes_DefaultResourceChunk, TwinRes_Item, layout7Section);
    defaultSerdes[7] = SECTION_SERDE(TwinRes_DefaultResourceChunk, TwinRes_LayoutSection, layout8Section);
    defaultSerdes[8] = SECTION_SERDE(TwinRes_DefaultResourceChunk, TwinRes_DefaultParticles, particles);
    defaultSerdes[9] = SECTION_SERDE(TwinRes_DefaultResourceChunk, TwinRes_Item, collision);
    defaultSerdes[10] = SECTION_SERDE(TwinRes_DefaultResourceChunk, TwinRes_CodeSection, codeSection);
    defaultSerdes[11] = SECTION_SERDE(TwinRes_DefaultResourceChunk, TwinRes_GraphicsSection, graphicsSection);

    codeSerdes[0] = SECTION_SERDE(TwinRes_CodeSection, TwinRes_ObjectsSection, objectsSection);
    codeSerdes[1] = SECTION_SERDE(TwinRes_CodeSection, TwinRes_AgentLabBehaviorsSection, behaviorsSection);
    codeSerdes[2] = SECTION_SERDE(TwinRes_CodeSection, TwinRes_AnimationsSection, animationsSection);
    codeSerdes[3] = SECTION_SERDE(TwinRes_CodeSection, TwinRes_BodiesSection, bodiesSection);
    codeSerdes[4] = SECTION_SERDE(TwinRes_CodeSection, TwinRes_AgentLabSequencesSection, sequencesSection);
    codeSerdes[5] = SECTION_SERDE(TwinRes_CodeSection, TwinRes_Item, unusedItem);
    // SFX's structure is unique, because ofc it is
    codeSerdes[6] = (TwinRes_SectionSerdeInfo) { .targetOffset = offsetof(TwinRes_CodeSection, soundEffectsSection), .deserialFunc = &TwinRes_SoundEffectsDeserializerWrapper, .serialFunc = &TwinRes_SoundEffectsSerializerWrapper };
    codeSerdes[7] = (TwinRes_SectionSerdeInfo) { .targetOffset = offsetof(TwinRes_CodeSection, englishEffectsSection), .deserialFunc = &TwinRes_SoundEffectsDeserializerWrapper, .serialFunc = &TwinRes_SoundEffectsSerializerWrapper };
    codeSerdes[8] = (TwinRes_SectionSerdeInfo) { .targetOffset = offsetof(TwinRes_CodeSection, frenchEffectsSection), .deserialFunc = &TwinRes_SoundEffectsDeserializerWrapper, .serialFunc = &TwinRes_SoundEffectsSerializerWrapper };
    codeSerdes[9] = (TwinRes_SectionSerdeInfo) { .targetOffset = offsetof(TwinRes_CodeSection, germanEffectsSection), .deserialFunc = &TwinRes_SoundEffectsDeserializerWrapper, .serialFunc = &TwinRes_SoundEffectsSerializerWrapper };
    codeSerdes[10] = (TwinRes_SectionSerdeInfo) { .targetOffset = offsetof(TwinRes_CodeSection, spanishEffectsSection), .deserialFunc = &TwinRes_SoundEffectsDeserializerWrapper, .serialFunc = &TwinRes_SoundEffectsSerializerWrapper };
    codeSerdes[11] = (TwinRes_SectionSerdeInfo) { .targetOffset = offsetof(TwinRes_CodeSection, italianEffectsSection), .deserialFunc = &TwinRes_SoundEffectsDeserializerWrapper, .serialFunc = &TwinRes_SoundEffectsSerializerWrapper };
    codeSerdes[12] = (TwinRes_SectionSerdeInfo) { .targetOffset = offsetof(TwinRes_CodeSection, japaneseEffectsSection), .deserialFunc = &TwinRes_SoundEffectsDeserializerWrapper, .serialFunc = &TwinRes_SoundEffectsSerializerWrapper };

    layoutSerdes[0] = SECTION_SERDE(TwinRes_LayoutSection, TwinRes_InstanceTemplatesSection, instanceTemplatesSection);
    layoutSerdes[1] = SECTION_SERDE(TwinRes_LayoutSection, TwinRes_AiPositionsSection, aiPositionsSection);
    layoutSerdes[2] = SECTION_SERDE(TwinRes_LayoutSection, TwinRes_AiPathsSection, aiPathsSection);
    layoutSerdes[3] = SECTION_SERDE(TwinRes_LayoutSection, TwinRes_PositionsSection, positionsSection);
    layoutSerdes[4] = SECTION_SERDE(TwinRes_LayoutSection, TwinRes_PathsSection, pathsSection);
    layoutSerdes[5] = SECTION_SERDE(TwinRes_LayoutSection, TwinRes_CollisionSurfacesSection, surfacesSection);
    layoutSerdes[6] = SECTION_SERDE(TwinRes_LayoutSection, TwinRes_InstancesSection, instancesSection);
    layoutSerdes[7] = SECTION_SERDE(TwinRes_LayoutSection, TwinRes_TriggersSection, triggersSection);
    layoutSerdes[8] = SECTION_SERDE(TwinRes_LayoutSection, TwinRes_CamerasSection, camerasSection);
}


void TwinRes_SectionSerialize(void* source, TwinRes_SectionSerdeInfo* serializers, TwinStudio_BinarySerializer* serializer, TwinStudio_Arena* arena, size_t size, void* userData)
{
    TwinRes_SectionHeader* header = source;
    const size_t baseOffset = TwinStudio_BinGetStreamPosition(serializer);
    header->itemsCount = arrlen(header->records);
    TwinRes_SectionHeaderBinSerialize(header, serializer, arena, 0, NULL);
    for (uint32_t i = 0; i < header->itemsCount; ++i)
    {
        TwinRes_ItemRecord* record = header->records + i;
        const size_t streamCurPos = TwinStudio_BinGetStreamPosition(serializer);
        record->offset = streamCurPos - baseOffset;
        TwinRes_SectionSerdeInfo* serde = serializers + record->itemId;
        serde->serialFunc((uint8_t*)(source) + serde->targetOffset, serializer, arena, record->size, source);
        record->size = TwinStudio_BinGetStreamPosition(serializer) - streamCurPos;
    }

    const size_t streamEndPos = TwinStudio_BinGetStreamPosition(serializer);
    const uint32_t resultLength = TwinStudio_BinGetStreamPosition(serializer) - baseOffset;
    header->length = resultLength;

    // Write the header again with updated offsets/sizes
    TwinStudio_BinSerializerSetPosition(serializer, baseOffset);
    TwinRes_SectionHeaderBinSerialize(header, serializer, arena, 0, NULL);
    TwinStudio_BinSerializerSetPosition(serializer, streamEndPos);
}


void TwinRes_SectionDeserialize(TwinStudio_DeserializationContext* ctx, void* target, TwinRes_SectionSerdeInfo* deserializers, TwinStudio_BinarySerializer* deserializer, TwinStudio_Arena* arena, size_t size, void* userData)
{
    TwinRes_SectionHeader* header = target;
    const uint32_t baseOffset = TwinStudio_BinGetStreamPosition(deserializer);
    TwinRes_SectionHeaderBinDeserialize(ctx, header, deserializer, arena, 0, NULL);
    for (uint32_t i = 0; i < header->itemsCount; ++i)
    {
        const TwinRes_ItemRecord* record = header->records + i;
        TwinStudio_BinSerializerSetPosition(deserializer, baseOffset + record->offset);

        TwinRes_SectionSerdeInfo* serde = deserializers + record->itemId;
        ctx->curItemTwinId = record->itemId;
        serde->deserialFunc(ctx, (uint8_t*)(target) + serde->targetOffset, deserializer, arena, record->size, target);
    }
}


void TwinRes_LayoutSectionBinSerialize(TwinRes_LayoutSection* source, TwinStudio_BinarySerializer* serializer, TwinStudio_Arena* arena, size_t size, void* userData)
{
    TwinRes_SectionSerialize(source, layoutSerdes, serializer, arena, size, userData);
}

void TwinRes_LayoutSectionBinDeserialize(TwinStudio_DeserializationContext* ctx, TwinRes_LayoutSection* target, TwinStudio_BinarySerializer* deserializer, TwinStudio_Arena* arena, size_t size, void* userData)
{
    TwinRes_SectionDeserialize(ctx, target, layoutSerdes, deserializer, arena, size, userData);
}

void TwinRes_CodeSectionBinSerialize(TwinRes_CodeSection* source, TwinStudio_BinarySerializer* serializer, TwinStudio_Arena* arena, size_t size, void* userData)
{
    TwinRes_SectionSerialize(source, codeSerdes, serializer, arena, size, userData);
}

void TwinRes_CodeSectionBinDeserialize(TwinStudio_DeserializationContext* ctx, TwinRes_CodeSection* target, TwinStudio_BinarySerializer* deserializer, TwinStudio_Arena* arena, size_t size, void* userData)
{
    TwinRes_SectionDeserialize(ctx, target, codeSerdes, deserializer, arena, size, userData);
}

void TwinRes_GraphicsSectionBinSerialize(TwinRes_GraphicsSection* source, TwinStudio_BinarySerializer* serializer, TwinStudio_Arena* arena, size_t size, void* userData)
{
    TwinRes_SectionSerialize(source, graphicsSerdes, serializer, arena, size, userData);
}

void TwinRes_GraphicsSectionBinDeserialize(TwinStudio_DeserializationContext* ctx, TwinRes_GraphicsSection* target, TwinStudio_BinarySerializer* deserializer, TwinStudio_Arena* arena, size_t size, void* userData)
{
    TwinRes_SectionDeserialize(ctx, target, graphicsSerdes, deserializer, arena, size, userData);
}

void TwinRes_ChunkBinSerialize(TwinRes_Chunk* source, TwinStudio_Arena* arena, TwinStudio_BinarySerializer* resourceSerializer, TwinStudio_BinarySerializer* scenerySerializer)
{
    TwinRes_ResourceChunkBinSerialize(&source->chunkResources, resourceSerializer, arena, 0, source);
    TwinRes_SceneryChunkBinSerialize(&source->sceneryResources, scenerySerializer, arena, 0, source);
}


void TwinRes_ChunkBinDeserialize(TwinStudio_DeserializationContext* ctx, TwinRes_Chunk* target, TwinStudio_Arena* arena, TwinStudio_BinarySerializer* resourceDeserializer, TwinStudio_BinarySerializer* sceneryDeserializer)
{
    TwinRes_ResourceChunkBinDeserialize(ctx, &target->chunkResources, resourceDeserializer, arena, 0, target);
    TwinRes_SceneryChunkBinDeserialize(ctx, &target->sceneryResources, sceneryDeserializer, arena, 0, target);
}


cJSON* TwinRes_ChunkJsonSerialize(TwinRes_Chunk* source)
{
    cJSON* rootJson = cJSON_CreateObject();
    cJSON_AddItemToObject(rootJson, "resources", TwinRes_ResourceChunkJsonSerialize(&source->chunkResources));
    cJSON_AddItemToObject(rootJson, "scenery", TwinRes_SceneryChunkJsonSerialize(&source->sceneryResources));
    return rootJson;
}


void TwinRes_ChunkJsonDeserialize(TwinRes_Chunk* target, cJSON* json)
{
    TwinRes_ResourceChunkJsonDeserialize(&target->chunkResources, cJSON_GetObjectItemCaseSensitive(json, "resources"));
    TwinRes_SceneryChunkJsonDeserialize(&target->sceneryResources, cJSON_GetObjectItemCaseSensitive(json, "scenery"));
    cJSON_Delete(json);
}


void TwinRes_DefaultResourcesBinSerialize(TwinRes_DefaultResources* source, TwinStudio_Arena* arena, TwinStudio_BinarySerializer* resourceSerializer)
{
    TwinRes_SectionSerialize(&source->defaultResources, defaultSerdes, resourceSerializer, arena, 0, NULL);
}

void TwinRes_DefaultResourcesBinDeserialize(TwinStudio_DeserializationContext* ctx, TwinRes_DefaultResources* target, TwinStudio_Arena* arena, TwinStudio_BinarySerializer* resourceDeserializer)
{
    TwinRes_SectionDeserialize(ctx, &target->defaultResources, defaultSerdes, resourceDeserializer, arena, 0, NULL);
}

cJSON* TwinRes_DefaultResourcesJsonSerialize(TwinRes_DefaultResources* source)
{
    cJSON* rootJson = cJSON_CreateObject();
    cJSON_AddItemToObject(rootJson, "globalResources", TwinRes_DefaultResourceChunkJsonSerialize(&source->defaultResources));
    return rootJson;
}

void TwinRes_DefaultResourcesJsonDeserialize(TwinRes_DefaultResources* target, cJSON* json)
{
    TwinRes_DefaultResourceChunkJsonDeserialize(&target->defaultResources, cJSON_GetObjectItemCaseSensitive(json, "globalResources"));
    cJSON_Delete(json);
}


void TwinRes_ResourceChunkBinSerialize(TwinRes_ResourceChunk* source, TwinStudio_BinarySerializer* serializer, TwinStudio_Arena* arena, size_t size, void* userData)
{
    TwinRes_SectionSerialize(source, rm2Serdes, serializer, arena, size, userData);
}

void TwinRes_ResourceChunkBinDeserialize(TwinStudio_DeserializationContext* ctx, TwinRes_ResourceChunk* target, TwinStudio_BinarySerializer* deserializer, TwinStudio_Arena* arena, size_t size, void* userData)
{
    TwinRes_SectionDeserialize(ctx, target, rm2Serdes, deserializer, arena, size, userData);
}

void TwinRes_DefaultResourceChunkBinSerialize(TwinRes_DefaultResourceChunk* source, TwinStudio_BinarySerializer* serializer, TwinStudio_Arena* arena, size_t size, void* userData)
{
    TwinRes_SectionSerialize(source, defaultSerdes, serializer, arena, size, userData);
}

void TwinRes_DefaultResourceChunkBinDeserialize(TwinStudio_DeserializationContext* ctx, TwinRes_DefaultResourceChunk* target, TwinStudio_BinarySerializer* deserializer, TwinStudio_Arena* arena, size_t size, void* userData)
{
    TwinRes_SectionDeserialize(ctx, target, defaultSerdes, deserializer, arena, size, userData);
}

void TwinRes_SceneryChunkBinSerialize(TwinRes_SceneryChunk* source, TwinStudio_BinarySerializer* serializer, TwinStudio_Arena* arena, size_t size, void* userData)
{
    TwinRes_SectionSerialize(source, sm2Serdes, serializer, arena, size, userData);
}

void TwinRes_SceneryChunkBinDeserialize(TwinStudio_DeserializationContext* ctx, TwinRes_SceneryChunk* target, TwinStudio_BinarySerializer* deserializer, TwinStudio_Arena* arena, size_t size, void* userData)
{
    TwinRes_SectionDeserialize(ctx, target, sm2Serdes, deserializer, arena, size, userData);
}
