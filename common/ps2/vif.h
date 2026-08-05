#ifndef TS_VIF_H
#define TS_VIF_H


#include <stdint.h>
#include "memory/memory.h"
#include "serialization/binary_serializer.h"
#include "gif_tag.h"
#include "defines/defines.h"


typedef enum
{
    NOP = 0b0000000,
    STCYCL = 0b0000001,
    OFFSET = 0b0000010,
    BASE = 0b0000011,
    ITOP = 0b0000100,
    STMOD = 0b0000101,
    MSKPATH3 = 0b0000110,
    MARK = 0b0000111,
    FLUSHE = 0b0010000,
    FLUSH = 0b0010001,
    FLUSHA = 0b0010011,
    MSCAL = 0b0010100,
    MSCNT = 0b0010111,
    MSCALF = 0b0010101,
    STMASK = 0b0100000,
    STROW = 0b0110000,
    STCOL = 0b0110001,
    MPG = 0b1001010,
    DIRECT = 0b1010000,
    DIRECTHL = 0b1010001,
    UNPACK = 0b1100000,
} TwinStudio_VIFCode;


typedef enum
{
    S_32 = 0b0000,
    S_16 = 0b0001,
    S_8 = 0b0010,
    V2_32 = 0b0100,
    V2_16 = 0b0101,
    V2_8 = 0b0110,
    V3_32 = 0b1000,
    V3_16 = 0b1001,
    V3_8 = 0b1010,
    V4_32 = 0b1100,
    V4_16 = 0b1101,
    V4_8 = 0b1110,
    V4_5 = 0b1111,
} TwinStudio_VIFPackFormat;


typedef struct TwinStudio_VIFInstruction {
    union {
        uint32_t fullInstruction;
        struct {
            union {
                uint16_t immediate;
                struct {
                    uint8_t cl;
                    uint8_t wl;
                } stcycl;
                struct {
                    uint16_t offset : 10;
                    uint8_t _pad : 6;
                } offset;
                struct {
                    uint16_t base : 10;
                    uint8_t _pad : 6;
                } base;
                struct {
                    uint16_t addr : 10;
                    uint8_t _pad : 6;
                } itop;
                struct {
                    uint8_t mode : 2;
                    uint16_t _pad : 14;
                } stmod;
                struct {
                    uint16_t _pad : 15;
                    uint8_t mask : 1;
                } mskpath3;
                struct {
                    uint16_t mark;
                } mark;
                struct {
                    uint16_t _;
                } flushe;
                struct {
                    uint16_t _;
                } flush;
                struct {
                    uint16_t _;
                } flusha;
                struct {
                    uint16_t execaddr;
                } mscal;
                struct {
                    uint16_t _;
                } mscnt;
                struct {
                    uint16_t execaddr;
                } mscalf;
                struct {
                    uint16_t _;
                } stmask;
                struct {
                    uint16_t _;
                } strow;
                struct {
                    uint16_t _;
                } stcol;
                struct {
                    uint16_t loadaddr;
                } mpg;
                struct {
                    uint16_t size;
                } direct;
                struct {
                    uint16_t size;
                } directhl;
                struct {
                    uint16_t addr : 10;
                    uint8_t  _pad : 4;
                    uint8_t usn   : 1;
                    uint8_t flg   : 1;
                } unpack;
            };
            uint8_t num;
            union {
                uint8_t command;
                TS_COMPACT_STRUCT {
                    TwinStudio_VIFCode cmd : 7;
                    uint8_t interrupt : 1;
                };
                struct {
                    uint8_t vl : 2;
                    uint8_t vn : 2;
                    uint8_t m : 1;
                    uint8_t ident : 2;
                    uint8_t interrupt : 1;
                } unpck;
            };
        };
    };
} TwinStudio_VIFInstruction;


typedef struct TwinStudio_VIFVector {
    union {
        struct {
            float x;
            float y;
            float z;
            float w;
        } floating;
        struct {
            uint32_t x;
            uint32_t y;
            uint32_t z;
            uint32_t w;
        } integer;
    };
} TwinStudio_VIFVector;


typedef struct TwinStudio_VIFInterpreter {
    uint32_t vifn_r[4];
    uint32_t vifn_c[4];
    uint32_t vifn_cycle;
    uint32_t vifn_mask;
    uint32_t vifn_mode;
    uint32_t vifn_itop;
    uint32_t vifn_itops;
    uint32_t vifn_base;
    uint32_t vifn_ofst;
    uint32_t vifn_top;
    uint32_t vifn_tops;
    uint32_t vifn_mark;
    uint32_t vifn_num;
    uint32_t vifn_code;
} TwinStudio_VIFInterpreter;


typedef struct TwinStudio_VIFOutput {
    TwinStudio_VIFVector  metaVectors[2];
    TwinStudio_VIFVector  scaleVector;
    uint32_t*             groupSizes;
    TwinStudio_VIFVector* vertexes;
    TwinStudio_VIFVector* blendFaceOffsets;
    TwinStudio_VIFVector* uvColors;
    TwinStudio_VIFVector* normals;
    TwinStudio_VIFVector* emits;
    TwinStudio_VIFVector* jointWeights;
    TwinStudio_ResultingGifTag* gifTags;
} TwinStudio_VIFOutput;


typedef struct TwinStudio_VIFCompilerOutput {
    uint8_t* packedBlendFaces;
    uint8_t* vifByteCode;
} TwinStudio_VIFCompilerOutput;


TwinStudio_VIFOutput          TwinStudio_VIFInterpret(TwinStudio_BinarySerializer* reader, TwinStudio_Arena* arena);
TwinStudio_VIFCompilerOutput  TwinStudio_VIFCompile(TwinStudio_VIFOutput* input);



#endif // TS_VIF_H