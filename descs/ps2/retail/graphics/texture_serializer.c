#include <raylib.h>
#include <cJSON.h>
#include <stdint.h>
#include <string.h>
#include "image/ez_swizzle.h"
#include "memory/memory.h"
#include "ps2/gif_tag.h"
#include "ps2/vif.h"
#include "auto_struct_texture.h"
#include "serialization/binary_serializer.h"
#include "texture_serialization.h"

TwinRes_TextureFormats formats;


static int32_t PaletteIndex(Color* palette, Color c, uint32_t paletteLength)
{
    uint32_t checkColorValue = ColorToInt(c);
    for (uint32_t i = 0; i < paletteLength; ++i)
    {
        uint32_t paletteColorValue = ColorToInt(palette[i]);
        if (checkColorValue == paletteColorValue)
        {
            return i;
        }
    }

    return -1;
}


static TwinRes_TextureFormat* GetSuitableTextureFormat(uint32_t width, uint32_t height)
{
    for (uint32_t i = 0; i < arrlen(formats.formats); ++i)
    {
        if (formats.formats[i].width == width && formats.formats[i].height == height)
        {
            return formats.formats + i;
        }
    }

    return NULL;
}


void TwinStudio_TextureSerializationInit()
{
    char* jsonFormatsText = LoadFileText("resources/ps2_texture_formats.json");
    cJSON* formatsJson = cJSON_Parse(jsonFormatsText);
    TwinRes_TextureFormatsJsonDeserialize(&formats, formatsJson);
    UnloadFileText(jsonFormatsText);
}


void ImageBinSerialize(Image* texture, TwinStudio_BinarySerializer* serializer, TwinStudio_Arena* arena, size_t size, void* userData)
{
    const uint32_t width = texture->width;
    const uint32_t height = texture->height;
    const uint32_t pixels = width * height;
    TwinStudio_Arena tempArena = TwinStudio_CreateArena(1024 * 1024);
    TwinRes_Texture* twinTexture = userData;
    TwinStudio_ResultingGifTagInput headerInput = { 0 };
    headerInput.gifTag.reg0 = TS_GS_REG_ApD;
    headerInput.gifTag.nloop = 3;
    headerInput.gifTag.nreg = 1;
    headerInput.gifTag.flg = PACKED;
    headerInput.inputsLength = 3;
    headerInput.inputs = TwinStudio_ArenaAlloc(&tempArena, sizeof(TwinStudio_GsRegInput) * 3);
    headerInput.inputs[0].apd.address = 81;
    headerInput.inputs[1].apd.address = 82;
    headerInput.inputs[2].apd.address = 83;

    TwinStudio_ResultingGifTagInput imageTag = { 0 };
    switch (twinTexture->textureFormat) {
        case 0: // PSMCT32
        {
            Color* scaledDownColors = TwinStudio_ArenaAlloc(&tempArena, sizeof(Color) * pixels);
            memcpy(scaledDownColors, texture->data, sizeof(Color) * pixels);
            for (uint32_t i = 0; i < pixels; ++i)
            {
                scaledDownColors[i].r >>= 1;
                scaledDownColors[i].g >>= 1;
                scaledDownColors[i].b >>= 1;
                scaledDownColors[i].a >>= 1;
            }
            imageTag = TwinStudio_ColorsToTag(scaledDownColors, pixels, &tempArena);
            break;
        }
        case 19: // PSMT8
        {
            uint8_t* textureData = TwinStudio_ArenaAlloc(&tempArena, pixels);
            uint8_t* paletteData = TwinStudio_ArenaAlloc(&tempArena, 256 * 4);
            Color* palette = TwinStudio_ArenaAlloc(&tempArena, 256 * sizeof(Color));
            uint32_t paletteIndex = 0;
            for (uint32_t i = 0; i < pixels; ++i)
            {
                Color pixel = ((Color*)texture->data)[i];
                // TODO: Image quantization to support more than 256 colors in an image by quantizing
                if (paletteIndex >= 256)
                {
                    break;
                }

                if (PaletteIndex(palette, pixel, paletteIndex) == -1)
                {
                    palette[paletteIndex++] = pixel;
                }
            }

            for (uint32_t i = 0; i < pixels; ++i)
            {
                uint32_t paletteIndex = PaletteIndex(palette, ((Color*)texture->data)[i], paletteIndex);
                if (paletteIndex == -1)
                {
                    textureData[i] = 0xFF;
                }
                else
                {
                    textureData[i] = paletteIndex;
                }
            }

            for (uint32_t i = 0; i < 256; ++i)
            {
                palette[i].r >>= 1;
                palette[i].g >>= 1;
                palette[i].b >>= 1;
                palette[i].a >>= 1;
            }

            for (uint32_t i = 0; i < 8; i++)
            {
                for (uint32_t j = 8; j < 16; j++)
                {
                    uint32_t srcIndex = j + i * 32 + 8;
                    uint32_t dstIndex = j + i * 32;
                    Color tmp = palette[srcIndex];
                    palette[srcIndex] = palette[dstIndex];
                    palette[dstIndex] = tmp;
                }
            }

            for (uint32_t i = 0; i < 256; ++i)
            {
                TwinStudio_ColorToBytes(palette[i], paletteData, i);
            }

            TwinRes_TextureFormat* format = GetSuitableTextureFormat(width, height);
            headerInput.inputs[1].apd.data32[0] = format->rrw;
            headerInput.inputs[1].apd.data32[1] = format->rrh;

            uint8_t* rawTextureData = TwinStudio_ArenaAlloc(&tempArena, format->rrw * 256);
            memset(rawTextureData, 0xFF, format->rrw * 256);

            TwinStudio_WriteTexPSMT8(rawTextureData, 0, twinTexture->textureBufferWidth, 0, 0, width, height, textureData);

            uint8_t* prevData = textureData;
            uint32_t mipWidth = width;
            uint32_t mipHeight = height;
            for (uint32_t i = 1; i < twinTexture->mipLevels; ++i)
            {
                uint32_t prevWidth = mipWidth;
                mipWidth /= 2;
                mipHeight /= 2;
                uint8_t* mipData = TwinStudio_ArenaAlloc(&tempArena, mipWidth * mipHeight);
                for (uint32_t y = 0; y < mipHeight; ++y)
                {
                    for (uint32_t x = 0; x < mipWidth; ++x)
                    {
                        uint32_t srcX = x * 2;
                        uint32_t srcY = y * 2;
                        mipData[x + y * mipWidth] = prevData[srcX + srcY * prevWidth];
                    }
                }

                TwinStudio_WriteTexPSMT8(rawTextureData, twinTexture->mipLevelsTBP[i - 1], twinTexture->mipLevelsTBW[i - 1], 0, 0, mipWidth, mipHeight, mipData);
                prevData = mipData;
            }
            TwinStudio_WriteTexPSMCT32(rawTextureData, twinTexture->clutBufferBasePointer, 1, 0, 0, 16, 16, paletteData);
            uint8_t* gifData = TwinStudio_ArenaAlloc(&tempArena, format->rrw * 256);
            TwinStudio_ReadTexPSMCT32(gifData, 0, 1, 0, 0, format->rrw, format->rrh, rawTextureData);
            imageTag = TwinStudio_ColorsToTag(TwinStudio_BytesToColors(gifData, format->rrw * format->rrh, &tempArena), (format->rrw * format->rrh) / 4, &tempArena);
            break;
        }
    }

    uint32_t qwc = 2 + 3 + imageTag.gifTag.nloop;
    uint64_t low = qwc;
    low |= 6 << 28;
    TwinStudio_BinWriteUInt64(serializer, low);
    TwinStudio_VIFInstruction nop = { .cmd = NOP };
    TwinStudio_BinWriteAny(serializer, &nop, sizeof(TwinStudio_VIFInstruction));

    TwinStudio_VIFInstruction direct = { .cmd = DIRECT };
    direct.direct.size = qwc;
    TwinStudio_BinWriteAny(serializer, &direct, sizeof(TwinStudio_VIFInstruction));
    TwinStudio_GifTagWrite(serializer, headerInput);
    TwinStudio_GifTagWrite(serializer, imageTag);

    TwinStudio_ArenaFree(&tempArena);
}

void ImageBinDeserialize(Image* texture, TwinStudio_BinarySerializer* deserializer, TwinStudio_Arena* arena, size_t size, void* userData)
{
    TwinStudio_Arena tempArena = TwinStudio_CreateArena(1024 * 1024);
    TwinRes_Texture* twinTexture = userData;
    texture->format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    texture->mipmaps = twinTexture->mipLevels;
    texture->width = 1 << twinTexture->imageWidthPower;
    texture->height = 1 << twinTexture->imageHeightPower;

    void* vifData = TwinStudio_BinReadBlob(deserializer, &tempArena, size);
    TwinStudio_BinarySerializer* vifReader = TwinStudio_BinSerializerAllocate(vifData, TwinStudio_BinarySerializerModeRead, size, false);
    TwinStudio_VIFOutput output = TwinStudio_VIFInterpret(vifReader, &tempArena);
    TwinStudio_BinSerializerFree(vifReader);

    switch (twinTexture->textureFormat) {
        case 0: // PSMCT32
        {
            Color* colors = TwinStudio_TagToColors(&output.gifTags[1], arena);
            for (uint32_t i = 0; i < texture->width * texture->height; ++i)
            {
                colors[i].r <<= 1;
                colors[i].g <<= 1;
                colors[i].b <<= 1;
                if (colors[i].a == 0x7F)
                {
                    colors[i].a = 0xFF;
                }
                else
                {
                    colors[i].a <<= 1;
                }
            }
            texture->data = colors;
            break;
        }
        case 19: // PSMT8
        {
            uint8_t* gifData = TwinStudio_TagToBytes(&output.gifTags[1], arena);
            int32_t rrw = output.gifTags[0].outputs[0].gsOutput[1].apd.data32[0];
            int32_t rrh = output.gifTags[0].outputs[0].gsOutput[1].apd.data32[1];
            int32_t width = texture->width;
            int32_t height = texture->height;
            const uint32_t allocSize = output.gifTags[1].outputsLength * 16;
            uint8_t* rawTextureData = TwinStudio_ArenaAlloc(&tempArena, allocSize);
            TwinStudio_WriteTexPSMCT32(rawTextureData, 0, 1, 0, 0, rrw, rrh, gifData);
            uint8_t* texData = TwinStudio_ArenaAlloc(&tempArena, allocSize);
            TwinStudio_ReadTexPSMT8(texData, 0, twinTexture->textureBufferWidth, 0, 0, width, height, rawTextureData);
            uint8_t* paletteData = TwinStudio_ArenaAlloc(&tempArena, allocSize);
            TwinStudio_ReadTexPSMCT32(paletteData, twinTexture->clutBufferBasePointer, 1, 0, 0, 16, 16, rawTextureData);
            Color* palette = TwinStudio_BytesToColors(paletteData, 16 * 16 * 4, &tempArena);
            for (uint32_t i = 0; i < 8; ++i)
            {
                for (uint32_t j = 8; j < 16; ++j)
                {
                    Color tmp = palette[j + i * 32];
                    palette[j + i * 32] = palette[j + i * 32 + 8];
                    palette[j + i * 32 + 8] = tmp;
                }
            }
            for (uint32_t i = 0; i < 256; ++i)
            {
                palette[i].r <<= 1;
                palette[i].g <<= 1;
                palette[i].b <<= 1;
                if (palette[i].a == 0x7F)
                {
                    palette[i].a = 0xFF;
                }
                else
                {
                    palette[i].a <<= 1;
                }
            }

            Color* resultPixels = TwinStudio_ArenaAlloc(arena, sizeof(Color) * width * height);
            for (uint32_t i = 0; i < width * height; ++i)
            {
                resultPixels[i] = palette[texData[i]];
            }
            texture->data = resultPixels;
            break;
        }
    }

    TwinStudio_ArenaFree(&tempArena);
}