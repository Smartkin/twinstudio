#ifndef TR_CHUNK_SERIALIZER_H
#define TR_CHUNK_SERIALIZER_H

#include <cJSON.h>
#include <stddef.h>
#include "auto_struct_chunk.h"
#include "memory/memory.h"
#include "serialization/binary_serializer.h"

typedef void (*TwinRes_SectionSerde)(void*, TwinStudio_BinarySerializer*, TwinStudio_Arena*, size_t, void*);

#define TS_WRAP_SERDE_IN_VOID(SERDE_FUNC) \
void TwinRes_SerdeWrap##SERDE_FUNC(void* target, TwinStudio_BinarySerializer* serial, TwinStudio_Arena* arena, size_t size, void* userData) \
{ \
    SERDE_FUNC(target, serial, arena, size, userData); \
}

#define TS_GET_SERDE_WRAP(SERDE_FUNC) &TwinRes_SerdeWrap##SERDE_FUNC

#define TS_GET_SERIAL_WRAP(SERDE_NAME) &TwinRes_SerdeWrap##SERDE_NAME##BinSerialize
#define TS_GET_DESERIAL_WRAP(SERDE_NAME) &TwinRes_SerdeWrap##SERDE_NAME##BinDeserialize

#define TS_WRAP_SERDE_IN_VOID_ALL(SERDE_NAME) \
TS_WRAP_SERDE_IN_VOID(SERDE_NAME##BinSerialize) \
TS_WRAP_SERDE_IN_VOID(SERDE_NAME##BinDeserialize)

typedef struct TwinRes_SectionSerdeInfo {
    TwinRes_SectionSerde serialFunc;
    TwinRes_SectionSerde deserialFunc;
    size_t targetOffset;
} TwinRes_SectionSerdeInfo;

void TwinRes_ChunkSerializationInit();

void TwinRes_SectionSerialize(void* source, TwinRes_SectionSerdeInfo* serializers, TwinStudio_BinarySerializer* serializer, TwinStudio_Arena* arena, size_t size, void* userData);
void TwinRes_SectionDeserialize(void* target, TwinRes_SectionSerdeInfo* deserializers, TwinStudio_BinarySerializer* deserializer, TwinStudio_Arena* arena, size_t size, void* userData);

void TwinRes_LayoutSectionBinSerialize(TwinRes_LayoutSection* source, TwinStudio_BinarySerializer* serializer, TwinStudio_Arena* arena, size_t size, void* userData);
void TwinRes_LayoutSectionBinDeserialize(TwinRes_LayoutSection* target, TwinStudio_BinarySerializer* deserializer, TwinStudio_Arena* arena, size_t size, void* userData);
void TwinRes_CodeSectionBinSerialize(TwinRes_CodeSection* source, TwinStudio_BinarySerializer* serializer, TwinStudio_Arena* arena, size_t size, void* userData);
void TwinRes_CodeSectionBinDeserialize(TwinRes_CodeSection* target, TwinStudio_BinarySerializer* deserializer, TwinStudio_Arena* arena, size_t size, void* userData);
void TwinRes_GraphicsSectionBinSerialize(TwinRes_GraphicsSection* source, TwinStudio_BinarySerializer* serializer, TwinStudio_Arena* arena, size_t size, void* userData);
void TwinRes_GraphicsSectionBinDeserialize(TwinRes_GraphicsSection* target, TwinStudio_BinarySerializer* deserializer, TwinStudio_Arena* arena, size_t size, void* userData);


void TwinRes_ChunkBinSerialize(TwinRes_Chunk* source, TwinStudio_Arena* arena, TwinStudio_BinarySerializer* resourceSerializer, TwinStudio_BinarySerializer* scenerySerializer);
void TwinRes_ChunkBinDeserialize(TwinRes_Chunk* target, TwinStudio_Arena* arena, TwinStudio_BinarySerializer* resourceDeserializer, TwinStudio_BinarySerializer* sceneryDeserializer);
cJSON* TwinRes_ChunkJsonSerialize(TwinRes_Chunk* source);
void TwinRes_ChunkJsonDeserialize(TwinRes_Chunk* target, cJSON* json);
void TwinRes_DefaultResourcesBinSerialize(TwinRes_DefaultResources* source, TwinStudio_Arena* arena, TwinStudio_BinarySerializer* resourceSerializer);
void TwinRes_DefaultResourcesBinDeserialize(TwinRes_DefaultResources* target, TwinStudio_Arena* arena, TwinStudio_BinarySerializer* resourceDeserializer);
cJSON* TwinRes_DefaultResourcesJsonSerialize(TwinRes_DefaultResources* source);
void TwinRes_DefaultResourcesJsonDeserialize(TwinRes_DefaultResources* target, cJSON* json);

void TwinRes_ResourceChunkBinSerialize(TwinRes_ResourceChunk* source, TwinStudio_BinarySerializer* serializer, TwinStudio_Arena* arena, size_t size, void* userData);
void TwinRes_ResourceChunkBinDeserialize(TwinRes_ResourceChunk* target, TwinStudio_BinarySerializer* deserializer, TwinStudio_Arena* arena, size_t size, void* userData);
void TwinRes_DefaultResourceChunkBinSerialize(TwinRes_DefaultResourceChunk* source, TwinStudio_BinarySerializer* serializer, TwinStudio_Arena* arena, size_t size, void* userData);
void TwinRes_DefaultResourceChunkBinDeserialize(TwinRes_DefaultResourceChunk* target, TwinStudio_BinarySerializer* deserializer, TwinStudio_Arena* arena, size_t size, void* userData);
void TwinRes_SceneryChunkBinSerialize(TwinRes_SceneryChunk* source, TwinStudio_BinarySerializer* serializer, TwinStudio_Arena* arena, size_t size, void* userData);
void TwinRes_SceneryChunkBinDeserialize(TwinRes_SceneryChunk* target, TwinStudio_BinarySerializer* deserializer, TwinStudio_Arena* arena, size_t size, void* userData);

#endif // TR_CHUNK_SERIALIZER_H