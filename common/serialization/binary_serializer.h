#ifndef TS_BIN_SERIALIZER_H
#define TS_BIN_SERIALIZER_H

#include "string_view/string_view.h"
#include "memory/memory.h"

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    TwinStudio_BinarySerializerModeRead,
    TwinStudio_BinarySerializerModeWrite
} TwinStudio_BinarySerializerMode;

typedef struct TwinStudio_BinarySerializer TwinStudio_BinarySerializer;

TwinStudio_BinarySerializer* TwinStudio_BinSerializerAllocate(void* data, TwinStudio_BinarySerializerMode mode, size_t size, bool isFileStream);
void TwinStudio_BinSerializerFree(TwinStudio_BinarySerializer* serializer);
void TwinStudio_BinSerializerSetPosition(TwinStudio_BinarySerializer* serializer, size_t newPos);
size_t TwinStudio_BinGetStreamPosition(TwinStudio_BinarySerializer* serializer);
size_t TwinStudio_BinGetStreamLength(TwinStudio_BinarySerializer* serializer);


void TwinStudio_BinWriteUInt8(TwinStudio_BinarySerializer* serializer, uint8_t data);
void TwinStudio_BinWriteUInt16(TwinStudio_BinarySerializer* serializer, uint16_t data);
void TwinStudio_BinWriteUInt32(TwinStudio_BinarySerializer* serializer, uint32_t data);
void TwinStudio_BinWriteUInt64(TwinStudio_BinarySerializer* serializer, uint64_t data);
void TwinStudio_BinWriteInt8(TwinStudio_BinarySerializer* serializer, int8_t data);
void TwinStudio_BinWriteInt16(TwinStudio_BinarySerializer* serializer, int16_t data);
void TwinStudio_BinWriteInt32(TwinStudio_BinarySerializer* serializer, int32_t data);
void TwinStudio_BinWriteInt64(TwinStudio_BinarySerializer* serializer, int64_t data);
void TwinStudio_BinWriteFloat(TwinStudio_BinarySerializer* serializer, float data);
void TwinStudio_BinWriteChar(TwinStudio_BinarySerializer* serializer, char data);
void TwinStudio_BinWriteChars(TwinStudio_BinarySerializer* serializer, const char* data, size_t size);
void TwinStudio_BinWriteString(TwinStudio_BinarySerializer* serializer, TwinStudio_StringView data, bool writeNullChar);
void TwinStudio_BinWriteBlob(TwinStudio_BinarySerializer* serializer, const uint8_t* data, size_t size);
void TwinStudio_BinWriteAny(TwinStudio_BinarySerializer* serializer, const void* data, size_t size);

uint8_t TwinStudio_BinReadUInt8(TwinStudio_BinarySerializer* serializer);
uint16_t TwinStudio_BinReadUInt16(TwinStudio_BinarySerializer* serializer);
uint32_t TwinStudio_BinReadUInt32(TwinStudio_BinarySerializer* serializer);
uint64_t TwinStudio_BinReadUInt64(TwinStudio_BinarySerializer* serializer);
int8_t TwinStudio_BinReadInt8(TwinStudio_BinarySerializer* serializer);
int16_t TwinStudio_BinReadInt16(TwinStudio_BinarySerializer* serializer);
int32_t TwinStudio_BinReadInt32(TwinStudio_BinarySerializer* serializer);
int64_t TwinStudio_BinReadInt64(TwinStudio_BinarySerializer* serializer);
float TwinStudio_BinReadFloat(TwinStudio_BinarySerializer* serializer);
char TwinStudio_BinReadChar(TwinStudio_BinarySerializer* serializer);
char* TwinStudio_BinReadChars(TwinStudio_BinarySerializer* serializer, TwinStudio_Arena* arena, size_t size);
TwinStudio_StringView TwinStudio_BinReadString(TwinStudio_BinarySerializer* serializer, TwinStudio_Arena* arena);
uint8_t* TwinStudio_BinReadBlob(TwinStudio_BinarySerializer* serializer, TwinStudio_Arena* arena, size_t size);
void* TwinStudio_BinReadStruct(TwinStudio_BinarySerializer* serializer, TwinStudio_Arena* arena, size_t size);
void  TwinStudio_BinReadStructDirect(TwinStudio_BinarySerializer* serializer, void* target, size_t size);
void  TwinStudio_BinReadVoid(TwinStudio_BinarySerializer* serializer, size_t size);


void  TwinStudio_BinWriteToFile(TwinStudio_StringView path, TwinStudio_BinarySerializer* serializer);
void  TwinStudio_BinWriteToFileC(const char* path, TwinStudio_BinarySerializer* serializer);
TwinStudio_BinarySerializer* TwinStudio_BinWriteToFileStream(TwinStudio_StringView path);
TwinStudio_BinarySerializer* TwinStudio_BinWriteToFileStreamC(const char* path);
TwinStudio_BinarySerializer* TwinStudio_BinReadFromFile(TwinStudio_StringView path, bool streamFile);

#endif // TS_BIN_SERIALIZER_H