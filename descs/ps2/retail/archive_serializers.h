#ifndef TR_ARCHIVE_SERIALIZERS_H
#define TR_ARCHIVE_SERIALIZERS_H

#include "auto_struct_mb_archive.h"
#include "auto_struct_bd_archive.h"
#include "memory/memory.h"
#include "serialization/binary_serializer.h"
#include <stddef.h>


void TwinRes_MbArchiveDeserialize(TwinRes_MbArchive* target, TwinStudio_BinarySerializer* deserializer, TwinStudio_Arena* arena, size_t size, void* userData);
void TwinRes_BdArchiveDeserialize(TwinRes_BdArchive* target, TwinStudio_BinarySerializer* deserializer, TwinStudio_Arena* arena, size_t size, void* userData);
TwinRes_MbRecord* TwinRes_MbArhiveIterateItem(TwinRes_MbArchive* target, TwinStudio_BinarySerializer* deserializer, TwinStudio_Arena* arena, size_t size, void* userData);
TwinRes_BdRecord* TwinRes_BdArhiveIterateItem(TwinRes_BdArchive* target, TwinStudio_BinarySerializer* deserializer, TwinStudio_Arena* arena, size_t size, void* userData);

#endif // TR_ARCHIVE_SERIALIZERS_H