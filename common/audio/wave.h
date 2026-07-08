#ifndef TS_WAVE_H
#define TS_WAVE_H

#include "defines/defines.h"
#include "serialization/binary_serializer.h"
#include "memory/memory.h"
#include "string_view/string_view.h"

typedef struct TwinStudio_Wave {
    void* data;
    uint32_t dataSize;
    uint32_t samplerate;
    uint8_t channels;
} TwinStudio_Wave;

void TwinStudio_WaveSaveToFile(TwinStudio_StringView path, TwinStudio_Wave wave);
TwinStudio_Wave* TwinStudio_WaveLoadFromFile(TwinStudio_StringView path, TwinStudio_Arena* arena);

#endif // TS_WAVE_H