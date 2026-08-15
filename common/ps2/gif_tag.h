#ifndef TS_GIF_TAG_H
#define TS_GIF_TAG_H

#include <stdint.h>
#include "defines/defines.h"
#include "memory/memory.h"
#include "serialization/binary_serializer.h"

typedef TS_COMPACT_ENUM {
    TS_GS_REG_PRIM = 0x00,
    TS_GS_REG_RGBAQ = 0x01,
    TS_GS_REG_ST = 0x02,
    TS_GS_REG_UV = 0x03,
    TS_GS_REG_XYZF2 = 0x04,
    TS_GS_REG_XYZ2 = 0x05,
    TS_GS_REG_TEX0_1 = 0x06,
    TS_GS_REG_TEX1_1 = 0x07,
    TS_GS_REG_CLAMP_1 = 0x08,
    TS_GS_REG_CLAMP_2 = 0x09,
    TS_GS_REG_FOG = 0x0a,
    TS_GS_REG_RESERVED = 0x0b,
    TS_GS_REG_XYZF3 = 0x0c,
    TS_GS_REG_XYZ3 = 0x0d,
    TS_GS_REG_ApD = 0x0e,
    TS_GS_REG_NOP = 0x0f,
    TS_GS_REG_HWREG = 0xff
} TwinStudio_GsRegsEnum;

typedef TS_COMPACT_ENUM {
        PACKED = 0b00,
        REGLIST = 0b01,
        IMAGE = 0b10,
        DISABLE = 0b11
} TwinStudio_GIFModeEnum;

typedef TS_COMPACT_STRUCT TwinStudio_PackedGsRegs {
    uint8_t reg0  : 4; //TwinStudio_GsRegsEnum
    uint8_t reg1  : 4; //TwinStudio_GsRegsEnum
} TwinStudio_PackedGsRegs;

typedef TS_COMPACT_STRUCT TwinStudio_GifTag {
    uint64_t nloop  : 15;
    uint64_t eop     : 1;
    uint64_t __pad  : 30;
    uint64_t pre     : 1;
    uint64_t prim   : 11;
    uint64_t flg     : 2; //TwinStudio_GIFModeEnum
    uint64_t nreg    : 4;
    union {
        uint64_t regs;
        TwinStudio_PackedGsRegs packedRegs[8];
        struct {
            uint8_t reg0  : 4; //TwinStudio_GsRegsEnum
            uint8_t reg1  : 4; //TwinStudio_GsRegsEnum
            uint8_t reg2  : 4; //TwinStudio_GsRegsEnum
            uint8_t reg3  : 4; //TwinStudio_GsRegsEnum
            uint8_t reg4  : 4; //TwinStudio_GsRegsEnum
            uint8_t reg5  : 4; //TwinStudio_GsRegsEnum
            uint8_t reg6  : 4; //TwinStudio_GsRegsEnum
            uint8_t reg7  : 4; //TwinStudio_GsRegsEnum
            uint8_t reg8  : 4; //TwinStudio_GsRegsEnum
            uint8_t reg9  : 4; //TwinStudio_GsRegsEnum
            uint8_t reg10 : 4; //TwinStudio_GsRegsEnum
            uint8_t reg11 : 4; //TwinStudio_GsRegsEnum
            uint8_t reg12 : 4; //TwinStudio_GsRegsEnum
            uint8_t reg13 : 4; //TwinStudio_GsRegsEnum
            uint8_t reg14 : 4; //TwinStudio_GsRegsEnum
            uint8_t reg15 : 4; //TwinStudio_GsRegsEnum
        };
    };
} TwinStudio_GifTag;

typedef TS_COMPACT_UNION TwinStudio_GsRegInput {
    struct {
        uint16_t prim : 11;
    } prim;
    struct {
        uint32_t r     : 8;
        uint32_t _pad1 : 24;
        uint32_t g     : 8;
        uint32_t _pad2 : 24;
        uint32_t b     : 8;
        uint32_t _pad3 : 24;
        uint32_t a     : 8;
        uint32_t _pad4 : 24;
    } rgbaq;
    struct {
        float s;
        float t;
        float q;
        uint32_t _pad;
    } st;
    struct {
        uint16_t u    : 14;
        uint32_t _pad : 18;
        uint16_t v    : 14;
    } uv;
    TS_COMPACT_STRUCT {
        int32_t x       : 16;
        uint32_t _pad1  : 16;
        int32_t y       : 16;
        uint32_t _pad2  : 16;
        uint32_t  _pad3 : 4;
        uint32_t z      : 24;
        uint32_t _pad4  : 7;
        uint32_t _pad5  : 1;
        uint32_t f      : 8;
    } xyzf2;
    struct {
        int16_t x;
        uint16_t _pad1;
        int16_t y;
        uint16_t _pad2;
        uint32_t z;
    } xyz2;
    TS_COMPACT_STRUCT {
        uint64_t _pad1 : 64;
        uint32_t _pad2 : 32;
        uint8_t _pad3  : 4;
        uint8_t f      : 8;
    } fog;
    struct {
        union {
            uint64_t data;
            uint32_t data32[2];
            uint16_t data16[4];
            uint8_t  data8[8];
        };
        uint8_t address;
    } apd;
    struct {
        uint64_t low;
        uint64_t high;
    } raw;
} TwinStudio_GsRegInput;


typedef union TwinStudio_GsRegOutput {
    struct {
        uint16_t prim : 11;
    } prim;
    struct {
        uint8_t r;
        uint8_t g;
        uint8_t b;
        uint8_t a;
        float   q;
    } rgbaq;
    struct {
        float s;
        float t;
    } st;
    struct {
        uint16_t u      : 14;
        uint16_t  _pad  : 2;
        uint16_t v      : 14;
    } uv;
    struct {
        int32_t x   : 16;
        int32_t y   : 16;
        uint32_t z  : 24;
        uint32_t  f : 8;
    } xyzf2;
    struct {
        int32_t x   : 16;
        int32_t y   : 16;
        uint32_t z  : 32;
    } xyz2;
    struct {
        uint64_t _pad   : 56;
        uint64_t  f     : 8;
    } fog;
    struct {
        union {
            uint64_t data;
            uint32_t data32[2];
            uint16_t data16[4];
            uint8_t  data8[8];
        };
    } apd;
} TwinStudio_GsRegOutput;


typedef struct TwinStudio_GifAddressOutput {
    TwinStudio_GsRegOutput gsOutput[2];
    uint8_t gsOuputsLength;
    uint8_t gsRegAddress;
} TwinStudio_GifAddressOutput;


typedef struct TwinStudio_ResultingGifTag {
    TwinStudio_GifTag gifTag;
    size_t outputsLength;
    TwinStudio_GifAddressOutput* outputs;
}TwinStudio_ResultingGifTag;

typedef struct TwinStudio_ResultingGifTagInput {
    TwinStudio_GifTag gifTag;
    size_t inputsLength;
    TwinStudio_GsRegInput* inputs;
}TwinStudio_ResultingGifTagInput;

TwinStudio_ResultingGifTag TwinStudio_GifTagRead(TwinStudio_BinarySerializer* serializer, TwinStudio_Arena* arena);
void TwinStudio_GifTagWrite(TwinStudio_BinarySerializer* writer, TwinStudio_ResultingGifTagInput gifTag);

#endif  // TS_GIF_TAG_H