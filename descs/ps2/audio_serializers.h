#ifndef TR_AUDIO_SERIALIZERS_H
#define TR_AUDIO_SERIALIZERS_H

#include "audio/wave.h"
#include "memory/memory.h"
#include "serialization/binary_serializer.h"
#include "serialization/helpers.h"
#include <stddef.h>


void TwinStudio_WaveBinSerialize(TwinStudio_Wave* source, TwinStudio_BinarySerializer* serializer, TwinStudio_Arena* arena, size_t size, void* userData);
void TwinStudio_WaveBinDeserialize(TwinStudio_DeserializationContext* ctx, TwinStudio_Wave* target, TwinStudio_BinarySerializer* deserializer, TwinStudio_Arena* arena, size_t size, void* userData);


#endif // TR_AUDIO_SERIALIZERS_H