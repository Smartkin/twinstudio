#ifndef TS_EZ_SWIZZLE_H
#define TS_EZ_SWIZZLE_H

#include <stdint.h>
#include <raylib.h>
#include "memory/memory.h"
#include "ps2/gif_tag.h"

void TwinStudio_WriteTexPSMT8(uint8_t* destination, int32_t dbp, int32_t dbw, int32_t dsax, int32_t dsay, int32_t rrw, int32_t rrh, uint8_t* source);
void TwinStudio_ReadTexPSMT8(uint8_t* destination, int32_t dbp, int32_t dbw, int32_t dsax, int32_t dsay, int32_t rrw, int32_t rrh, uint8_t* source);

void TwinStudio_WriteTexPSMCT16(uint8_t* destination, int32_t dbp, int32_t dbw, int32_t dsax, int32_t dsay, int32_t rrw, int32_t rrh, uint8_t* source);
void TwinStudio_ReadTexPSMCT16(uint8_t* destination, int32_t dbp, int32_t dbw, int32_t dsax, int32_t dsay, int32_t rrw, int32_t rrh, uint8_t* source);

void TwinStudio_WriteTexPSMCT32(uint8_t* destination, int32_t dbp, int32_t dbw, int32_t dsax, int32_t dsay, int32_t rrw, int32_t rrh, uint8_t* source);
void TwinStudio_ReadTexPSMCT32(uint8_t* destination, int32_t dbp, int32_t dbw, int32_t dsax, int32_t dsay, int32_t rrw, int32_t rrh, uint8_t* source);

Color* TwinStudio_TagToColors(TwinStudio_ResultingGifTag* tag, TwinStudio_Arena* arena);
TwinStudio_ResultingGifTagInput TwinStudio_ColorsToTag(Color* colors, uint32_t colorsAmount, TwinStudio_Arena* arena);
uint8_t* TwinStudio_TagToBytes(TwinStudio_ResultingGifTag* tag, TwinStudio_Arena* arena);
void TwinStudio_ColorToBytes(Color color, uint8_t* dst, uint32_t index);
Color TwinStudio_BytesToColor(uint8_t* src, uint32_t index);
Color* TwinStudio_BytesToColors(uint8_t* src, uint32_t srcLength, TwinStudio_Arena* arena);


#endif // TS_EZ_SWIZZLE_H