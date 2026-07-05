#include "binary_serializer.h"
#include "memory/memory.h"
#include "rpmalloc.h"
#include "string_view/string_view.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <raylib.h>

struct TwinStudio_BinarySerializer
{
    size_t dataIndex;
    size_t size;
    union {
        void* data;
        uint8_t* byteData;
        FILE* fileStream;
    };
    TwinStudio_BinarySerializerMode mode;
    uint8_t allocatedIdx;
    bool    deallocDataOnFree;
    bool    isStream;
};

typedef struct SerializerPool {
    TwinStudio_BinarySerializer serializers[64];
    uint64_t allocatedFlags;
} SerializerPool;

static SerializerPool serializerPool;


static inline void AdvanceSerializer(TwinStudio_BinarySerializer* serializer, size_t amount)
{
    assert(serializer->dataIndex + amount < serializer->size);
    serializer->dataIndex += amount;
}


static inline void* GetDataHead(TwinStudio_BinarySerializer* serializer)
{
    return serializer->byteData + serializer->dataIndex;
}


static uint8_t GetFirstFreeSerializer()
{
    uint64_t mask = serializerPool.allocatedFlags;
    uint8_t freeIdx = 0;
    while (mask & 0x1)
    {
        freeIdx++;
        assert(freeIdx < 64);
        mask >>= 1;
    }

    return freeIdx;
}


inline void TwinStudio_BinSerializerSetPosition(TwinStudio_BinarySerializer* serializer, size_t newPos)
{
    if (serializer->isStream)
    {
        fseek(serializer->fileStream, newPos, SEEK_SET);
        return;
    }

    serializer->dataIndex = newPos;
}


inline size_t TwinStudio_BinGetStreamPosition(TwinStudio_BinarySerializer* serializer)
{
    if (serializer->isStream)
    {
        return ftell(serializer->fileStream);
    }

    return serializer->dataIndex;
}


inline size_t TwinStudio_BinGetStreamLength(TwinStudio_BinarySerializer* serializer)
{
    return serializer->size;
}


TwinStudio_BinarySerializer* TwinStudio_BinSerializerAllocate(void* data, TwinStudio_BinarySerializerMode mode, size_t size, bool isFileStream)
{
    uint8_t allocatedIdx = GetFirstFreeSerializer();
    serializerPool.serializers[allocatedIdx] = (TwinStudio_BinarySerializer) { .dataIndex = 0, .mode = mode, .size = size, .data = data, .allocatedIdx = allocatedIdx, .isStream = isFileStream };
    serializerPool.allocatedFlags |= (1 << allocatedIdx);
    return serializerPool.serializers + allocatedIdx;
}


void TwinStudio_BinSerializerFree(TwinStudio_BinarySerializer* serializer)
{
    serializerPool.allocatedFlags &= ~(1 << serializer->allocatedIdx);
    if (serializer->deallocDataOnFree)
    {
        rpfree(serializer->data);
        serializer->data = NULL;
        serializer->deallocDataOnFree = false;
    }

    if (serializer->isStream)
    {
        fclose(serializer->fileStream);
        serializer->fileStream = NULL;
        serializer->isStream = false;
    }
}


void TwinStudio_BinWriteUInt8(TwinStudio_BinarySerializer* serializer, uint8_t data)
{
    assert(serializer->mode == TwinStudio_BinarySerializerModeWrite);

    if (serializer->isStream)
    {
        fwrite(&data, sizeof(uint8_t), 1, serializer->fileStream);
        return;
    }

    *((uint8_t*)GetDataHead(serializer)) = data;
    AdvanceSerializer(serializer, sizeof(uint8_t));
}


void TwinStudio_BinWriteUInt16(TwinStudio_BinarySerializer* serializer, uint16_t data)
{
    assert(serializer->mode == TwinStudio_BinarySerializerModeWrite);

    if (serializer->isStream)
    {
        fwrite(&data, sizeof(uint16_t), 1, serializer->fileStream);
        return;
    }

    *((uint16_t*)GetDataHead(serializer)) = data;
    AdvanceSerializer(serializer, sizeof(uint16_t));
}


void TwinStudio_BinWriteUInt32(TwinStudio_BinarySerializer* serializer, uint32_t data)
{
    assert(serializer->mode == TwinStudio_BinarySerializerModeWrite);

    if (serializer->isStream)
    {
        fwrite(&data, sizeof(uint32_t), 1, serializer->fileStream);
        return;
    }

    *((uint32_t*)GetDataHead(serializer)) = data;
    AdvanceSerializer(serializer, sizeof(uint32_t));
}


void TwinStudio_BinWriteUInt64(TwinStudio_BinarySerializer* serializer, uint64_t data)
{
    assert(serializer->mode == TwinStudio_BinarySerializerModeWrite);

    if (serializer->isStream)
    {
        fwrite(&data, sizeof(uint64_t), 1, serializer->fileStream);
        return;
    }

    *((uint64_t*)GetDataHead(serializer)) = data;
    AdvanceSerializer(serializer, sizeof(uint64_t));
}


void TwinStudio_BinWriteInt8(TwinStudio_BinarySerializer* serializer, int8_t data)
{
    assert(serializer->mode == TwinStudio_BinarySerializerModeWrite);

    if (serializer->isStream)
    {
        fwrite(&data, sizeof(int8_t), 1, serializer->fileStream);
        return;
    }

    *((int8_t*)GetDataHead(serializer)) = data;
    AdvanceSerializer(serializer, sizeof(int8_t));
}


void TwinStudio_BinWriteInt16(TwinStudio_BinarySerializer* serializer, int16_t data)
{
    assert(serializer->mode == TwinStudio_BinarySerializerModeWrite);

    if (serializer->isStream)
    {
        fwrite(&data, sizeof(int16_t), 1, serializer->fileStream);
        return;
    }

    *((int16_t*)GetDataHead(serializer)) = data;
    AdvanceSerializer(serializer, sizeof(int16_t));
}


void TwinStudio_BinWriteInt32(TwinStudio_BinarySerializer* serializer, int32_t data)
{
    assert(serializer->mode == TwinStudio_BinarySerializerModeWrite);

    if (serializer->isStream)
    {
        fwrite(&data, sizeof(int32_t), 1, serializer->fileStream);
        return;
    }

    *((int32_t*)GetDataHead(serializer)) = data;
    AdvanceSerializer(serializer, sizeof(int32_t));
}


void TwinStudio_BinWriteInt64(TwinStudio_BinarySerializer* serializer, int64_t data)
{
    assert(serializer->mode == TwinStudio_BinarySerializerModeWrite);

    if (serializer->isStream)
    {
        fwrite(&data, sizeof(int64_t), 1, serializer->fileStream);
        return;
    }

    *((int64_t*)GetDataHead(serializer)) = data;
    AdvanceSerializer(serializer, sizeof(int64_t));
}


void TwinStudio_BinWriteFloat(TwinStudio_BinarySerializer* serializer, float data)
{
    assert(serializer->mode == TwinStudio_BinarySerializerModeWrite);

    if (serializer->isStream)
    {
        fwrite(&data, sizeof(float), 1, serializer->fileStream);
        return;
    }

    *((float*)GetDataHead(serializer)) = data;
    AdvanceSerializer(serializer, sizeof(float));
}


void TwinStudio_BinWriteChar(TwinStudio_BinarySerializer* serializer, char data)
{
    assert(serializer->mode == TwinStudio_BinarySerializerModeWrite);

    if (serializer->isStream)
    {
        fwrite(&data, sizeof(char), 1, serializer->fileStream);
        return;
    }

    *((char*)GetDataHead(serializer)) = data;
    AdvanceSerializer(serializer, sizeof(char));
}


void TwinStudio_BinWriteChars(TwinStudio_BinarySerializer* serializer, const char* data, size_t size)
{
    assert(serializer->mode == TwinStudio_BinarySerializerModeWrite);

    if (serializer->isStream)
    {
        fwrite(data, size, 1, serializer->fileStream);
        return;
    }

    memcpy(GetDataHead(serializer), data, size);
    AdvanceSerializer(serializer, size);
}


void TwinStudio_BinWriteString(TwinStudio_BinarySerializer* serializer, TwinStudio_StringView data, bool writeNullChar)
{
    assert(serializer->mode == TwinStudio_BinarySerializerModeWrite);

    TwinStudio_BinWriteUInt32(serializer, writeNullChar ? data.length + 1 : data.length);
    TwinStudio_BinWriteChars(serializer, data.string, data.length);
    if (writeNullChar)
    {
        TwinStudio_BinWriteChar(serializer, '\0');
    }
}


void TwinStudio_BinWriteBlob(TwinStudio_BinarySerializer* serializer, const uint8_t* data, size_t size)
{
    assert(serializer->mode == TwinStudio_BinarySerializerModeWrite);

    if (serializer->isStream)
    {
        fwrite(data, size, 1, serializer->fileStream);
        return;
    }

    memcpy(GetDataHead(serializer), data, size);
    AdvanceSerializer(serializer, size);
}


void TwinStudio_BinWriteAny(TwinStudio_BinarySerializer* serializer, const void* data, size_t size)
{
    assert(serializer->mode == TwinStudio_BinarySerializerModeWrite);

    if (serializer->isStream)
    {
        fwrite(data, size, 1, serializer->fileStream);
        return;
    }

    memcpy(GetDataHead(serializer), data, size);
    AdvanceSerializer(serializer, size);
}

uint8_t TwinStudio_BinReadUInt8(TwinStudio_BinarySerializer* serializer)
{
    assert(serializer->mode == TwinStudio_BinarySerializerModeRead);

    uint8_t result;
    if (serializer->isStream)
    {
        fread(&result, sizeof(uint8_t), 1, serializer->fileStream);
        return result;
    }

    result = *(uint8_t*)GetDataHead(serializer);
    AdvanceSerializer(serializer, sizeof(uint8_t));
    return result;
}


uint16_t TwinStudio_BinReadUInt16(TwinStudio_BinarySerializer* serializer)
{
    assert(serializer->mode == TwinStudio_BinarySerializerModeRead);

    uint16_t result;
    if (serializer->isStream)
    {
        fread(&result, sizeof(uint16_t), 1, serializer->fileStream);
        return result;
    }

    result = *(uint16_t*)GetDataHead(serializer);
    AdvanceSerializer(serializer, sizeof(uint16_t));
    return result;
}


uint32_t TwinStudio_BinReadUInt32(TwinStudio_BinarySerializer* serializer)
{
    assert(serializer->mode == TwinStudio_BinarySerializerModeRead);

    uint32_t result;
    if (serializer->isStream)
    {
        fread(&result, sizeof(uint32_t), 1, serializer->fileStream);
        return result;
    }

    result = *(uint32_t*)GetDataHead(serializer);
    AdvanceSerializer(serializer, sizeof(uint32_t));
    return result;
}


uint64_t TwinStudio_BinReadUInt64(TwinStudio_BinarySerializer* serializer)
{
    assert(serializer->mode == TwinStudio_BinarySerializerModeRead);

    uint64_t result;
    if (serializer->isStream)
    {
        fread(&result, sizeof(uint64_t), 1, serializer->fileStream);
        return result;
    }

    result = *(uint64_t*)GetDataHead(serializer);
    AdvanceSerializer(serializer, sizeof(uint64_t));
    return result;
}


int8_t TwinStudio_BinReadInt8(TwinStudio_BinarySerializer* serializer)
{
    assert(serializer->mode == TwinStudio_BinarySerializerModeRead);

    int8_t result;
    if (serializer->isStream)
    {
        fread(&result, sizeof(int8_t), 1, serializer->fileStream);
        return result;
    }

    result = *(int8_t*)GetDataHead(serializer);
    AdvanceSerializer(serializer, sizeof(int8_t));
    return result;
}


int16_t TwinStudio_BinReadInt16(TwinStudio_BinarySerializer* serializer)
{
    assert(serializer->mode == TwinStudio_BinarySerializerModeRead);

    int16_t result;
    if (serializer->isStream)
    {
        fread(&result, sizeof(int16_t), 1, serializer->fileStream);
        return result;
    }

    result = *(int16_t*)GetDataHead(serializer);
    AdvanceSerializer(serializer, sizeof(int16_t));
    return result;
}


int32_t TwinStudio_BinReadInt32(TwinStudio_BinarySerializer* serializer)
{
    assert(serializer->mode == TwinStudio_BinarySerializerModeRead);

    int32_t result;
    if (serializer->isStream)
    {
        fread(&result, sizeof(int32_t), 1, serializer->fileStream);
        return result;
    }

    result = *(int32_t*)GetDataHead(serializer);
    AdvanceSerializer(serializer, sizeof(int32_t));
    return result;
}


int64_t TwinStudio_BinReadInt64(TwinStudio_BinarySerializer* serializer)
{
    assert(serializer->mode == TwinStudio_BinarySerializerModeRead);

    int64_t result;
    if (serializer->isStream)
    {
        fread(&result, sizeof(int64_t), 1, serializer->fileStream);
        return result;
    }

    result = *(int64_t*)GetDataHead(serializer);
    AdvanceSerializer(serializer, sizeof(int64_t));
    return result;
}


float TwinStudio_BinReadFloat(TwinStudio_BinarySerializer* serializer)
{
    assert(serializer->mode == TwinStudio_BinarySerializerModeRead);

    float result;
    if (serializer->isStream)
    {
        fread(&result, sizeof(float), 1, serializer->fileStream);
        return result;
    }

    result = *(float*)GetDataHead(serializer);
    AdvanceSerializer(serializer, sizeof(float));
    return result;
}


char TwinStudio_BinReadChar(TwinStudio_BinarySerializer* serializer)
{
    assert(serializer->mode == TwinStudio_BinarySerializerModeRead);

    char result;
    if (serializer->isStream)
    {
        fread(&result, sizeof(char), 1, serializer->fileStream);
        return result;
    }

    result = *(char*)GetDataHead(serializer);
    AdvanceSerializer(serializer, sizeof(char));
    return result;
}


char* TwinStudio_BinReadChars(TwinStudio_BinarySerializer* serializer, TwinStudio_Arena* arena, size_t size)
{
    assert(serializer->mode == TwinStudio_BinarySerializerModeRead);

    char* chars = TwinStudio_ArenaAlloc(arena, size);

    if (serializer->isStream)
    {
        fread(chars, size, 1, serializer->fileStream);
        return chars;
    }

    memcpy(chars, GetDataHead(serializer), size);
    AdvanceSerializer(serializer, size);
    return chars;
}


TwinStudio_StringView TwinStudio_BinReadString(TwinStudio_BinarySerializer* serializer, TwinStudio_Arena* arena)
{
    assert(serializer->mode == TwinStudio_BinarySerializerModeRead);

    uint32_t strLen = TwinStudio_BinReadUInt32(serializer);
    char* chars = TwinStudio_BinReadChars(serializer, arena, strLen);
    return (TwinStudio_StringView) { .dynString = chars, .isDynamicallyAllocated = true, .length = strLen, .allocatedLength = strLen };
}


uint8_t* TwinStudio_BinReadBlob(TwinStudio_BinarySerializer* serializer, TwinStudio_Arena* arena, size_t size)
{
    assert(serializer->mode == TwinStudio_BinarySerializerModeRead);

    uint8_t* chars = TwinStudio_ArenaAlloc(arena, size);

    if (serializer->isStream)
    {
        fread(chars, size, 1, serializer->fileStream);
        return chars;
    }

    memcpy(chars, GetDataHead(serializer), size);
    AdvanceSerializer(serializer, size);
    return chars;
}


void* TwinStudio_BinReadStruct(TwinStudio_BinarySerializer* serializer, TwinStudio_Arena* arena, size_t size)
{
    assert(serializer->mode == TwinStudio_BinarySerializerModeRead);

    void* data = TwinStudio_ArenaAlloc(arena, size);
    if (serializer->isStream)
    {
        fread(data, size, 1, serializer->fileStream);
        return data;
    }

    memcpy(data, GetDataHead(serializer), size);
    AdvanceSerializer(serializer, size);
    return data;
}

void TwinStudio_BinReadStructDirect(TwinStudio_BinarySerializer* serializer, void* target, size_t size)
{
    assert(serializer->mode == TwinStudio_BinarySerializerModeRead);

    if (serializer->isStream)
    {
        fread(target, size, 1, serializer->fileStream);
        return;
    }

    memcpy(target, GetDataHead(serializer), size);
    AdvanceSerializer(serializer, size);
}

void TwinStudio_BinReadVoid(TwinStudio_BinarySerializer* serializer, size_t size)
{
    assert(serializer->mode == TwinStudio_BinarySerializerModeRead);

    if (serializer->isStream)
    {
        fseek(serializer->fileStream, size, SEEK_CUR);
        return;
    }

    AdvanceSerializer(serializer, size);
}

void TwinStudio_BinWriteToFile(TwinStudio_StringView path, TwinStudio_BinarySerializer* serializer)
{
    FILE* f = fopen(TwinStudio_GetCString(&path), "wb");
    fwrite(serializer->data, serializer->dataIndex + 1, 1, f);
    fclose(f);
}


TwinStudio_BinarySerializer* TwinStudio_BinReadFromFile(TwinStudio_StringView path, bool streamFile)
{
    const char* cPath = TwinStudio_GetCString(&path);
    const uint64_t fileSize = GetFileLength(cPath);
    FILE* f = fopen(cPath, "rb");
    if (!streamFile)
    {
        void* data = rpmalloc(fileSize);
        fread(data, fileSize, 1, f);
        fclose(f);

        TwinStudio_BinarySerializer* serializer = TwinStudio_BinSerializerAllocate(data, TwinStudio_BinarySerializerModeRead, fileSize, false);
        serializer->deallocDataOnFree = true;
        return serializer;
    }

    TwinStudio_BinarySerializer* serializer = TwinStudio_BinSerializerAllocate(f, TwinStudio_BinarySerializerModeRead, fileSize, true);
    return serializer;
}
