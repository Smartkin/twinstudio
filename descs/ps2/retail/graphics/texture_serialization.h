#ifndef TR_TEXTURE_SERIALIZATION_H
#define TR_TEXTURE_SERIALIZATION_H

#include <stddef.h>
#include <raylib.h>
#include "serialization/binary_serializer.h"
#include "memory/memory.h"

void ImageBinSerialize(Image* texture, TwinStudio_BinarySerializer* serializer, TwinStudio_Arena* arena, size_t size, void* userData);
void ImageBinDeserialize(Image* texture, TwinStudio_BinarySerializer* deserializer, TwinStudio_Arena* arena, size_t size, void* userData);

void TwinStudio_TextureSerializationInit();

#endif // TR_TEXTURE_SERIALIZATION_H