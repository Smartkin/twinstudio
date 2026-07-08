#include <limits.h>
#include <stdio.h>
#include <raylib.h>
#include "wave.h"
#include "defines/defines.h"
#include "memory/memory.h"
#include "serialization/binary_serializer.h"
#include "string_view/string_view.h"

void TwinStudio_WaveSaveToFile(TwinStudio_StringView path, TwinStudio_Wave wave)
{
    FILE* f = fopen(TwinStudio_GetCString(&path), "wb");
    TwinStudio_BinarySerializer* serializer = TwinStudio_BinSerializerAllocate(f, TwinStudio_BinarySerializerModeWrite, UINT_MAX, true);
    TwinStudio_BinWriteChars(serializer, "RIFF", 4);
    TwinStudio_BinWriteInt32(serializer, 36 + wave.dataSize);
    TwinStudio_BinWriteChars(serializer, "WAVE", 4);
    TwinStudio_BinWriteChars(serializer, "fmt ", 4);
    TwinStudio_BinWriteInt32(serializer, 16);
    TwinStudio_BinWriteUInt16(serializer, 1);
    TwinStudio_BinWriteInt16(serializer, wave.channels);
    TwinStudio_BinWriteUInt32(serializer, wave.samplerate);
    TwinStudio_BinWriteUInt32(serializer, wave.samplerate * wave.channels * 2);
    TwinStudio_BinWriteInt16(serializer, wave.channels * 2);
    TwinStudio_BinWriteUInt16(serializer, 16);
    TwinStudio_BinWriteChars(serializer, "data", 4);
    TwinStudio_BinWriteInt32(serializer, wave.dataSize);
    TwinStudio_BinWriteBlob(serializer, wave.data, wave.dataSize);
    TwinStudio_BinSerializerFree(serializer); // Also handles closing the file since ownership to data is transfered to the serializer
}


TwinStudio_Wave* TwinStudio_WaveLoadFromFile(TwinStudio_StringView path, TwinStudio_Arena* arena)
{
    uint32_t fileSize = GetFileLength(TwinStudio_GetCString(&path));
    TwinStudio_BinarySerializer* serializer = TwinStudio_BinReadFromFile(path, false);
    TwinStudio_Wave* wave = TwinStudio_ArenaAlloc(arena, sizeof(TwinStudio_Wave));
    TwinStudio_BinReadVoid(serializer, 22);
    wave->channels = TwinStudio_BinReadInt16(serializer);
    wave->samplerate = TwinStudio_BinReadUInt32(serializer);
    TwinStudio_BinReadVoid(serializer, 12);
    wave->dataSize = TwinStudio_BinReadInt32(serializer);
    wave->data = TwinStudio_BinReadBlob(serializer, arena, wave->dataSize);
    TwinStudio_BinSerializerFree(serializer);

    return wave;
}
