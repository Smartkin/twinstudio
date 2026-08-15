#include "vif.h"
#include "defines/defines.h"
#include "gif_tag.h"
#include "dma_tag.h"
#include "memory/memory.h"
#include "serialization/binary_serializer.h"
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stb_ds.h>
#include <assert.h>
#include <string.h>

static inline uint32_t SEXT16(uint32_t val, bool usn)
{
    if (usn)
    {
        return val;
    }

    return ((val & 0x8000) != 0) ? (val | 0xFFFF0000) : val;
}


static inline uint32_t SEXT8(uint32_t val, bool usn)
{
    if (usn)
    {
        return val;
    }

    return ((val & 0x80) != 0) ? (val | 0xFFFFFF00) : val;
}


typedef TS_COMPACT_STRUCT CompactColor {
    uint16_t r : 5;
    uint16_t g : 5;
    uint16_t b : 5;
    uint16_t a : 1;
} CompactColor;


typedef union CompactVector {
    uint32_t value;
    struct {
        uint16_t lo;
        uint16_t hi;
    };
    struct {
        uint8_t b1;
        uint8_t b2;
        uint8_t b3;
        uint8_t b4;
    };
    struct {
        CompactColor c1;
        CompactColor c2;
    };
} CompactVector;


static void Unpack(CompactVector* stack, TwinStudio_VIFVector* storage, TwinStudio_VIFInterpreter* interpreter, TwinStudio_BinarySerializer* reader, TwinStudio_VIFPackFormat packFormat, uint8_t amount, bool usn)
{
    uint32_t srcIdx = 0;
    switch (packFormat)
    {
        case S_32:
        {
            for (uint32_t i = 0; i < amount; ++i)
            {
                TwinStudio_VIFVector vec = { 0 };
                vec.integer.x = stack[i].value;
                vec.integer.y = stack[i].value;
                vec.integer.z = stack[i].value;
                vec.integer.w = stack[i].value;
                storage[i] = vec;
            }
            break;
        }
        case S_16:
        {
            for (uint32_t i = 0; i < amount; i += 2)
            {
                TwinStudio_VIFVector vec1 = { 0 };
                TwinStudio_VIFVector vec2 = { 0 };
                const uint32_t w1 = SEXT16(stack[srcIdx].lo, usn);
                const uint32_t w2 = SEXT16(stack[srcIdx].hi, usn);
                vec1.integer.x = w1;
                vec1.integer.y = w1;
                vec1.integer.z = w1;
                vec1.integer.w = w1;
                vec2.integer.x = w2;
                vec2.integer.y = w2;
                vec2.integer.z = w2;
                vec2.integer.w = w2;
                storage[i + 0] = vec1;
                if (amount >= 2)
                {
                    storage[i + 1] = vec2;
                }
                srcIdx++;
            }
            break;
        }
        case S_8:
        {
            for (uint32_t i = 0; i < amount; i += 4)
            {
                TwinStudio_VIFVector vec1 = { 0 };
                TwinStudio_VIFVector vec2 = { 0 };
                TwinStudio_VIFVector vec3 = { 0 };
                TwinStudio_VIFVector vec4 = { 0 };
                const uint32_t w1 = SEXT8(stack[srcIdx].b1, usn);
                const uint32_t w2 = SEXT8(stack[srcIdx].b2, usn);
                const uint32_t w3 = SEXT8(stack[srcIdx].b3, usn);
                const uint32_t w4 = SEXT8(stack[srcIdx].b4, usn);
                vec1.integer.x = w1;
                vec1.integer.y = w1;
                vec1.integer.z = w1;
                vec1.integer.w = w1;
                vec2.integer.x = w2;
                vec2.integer.y = w2;
                vec2.integer.z = w2;
                vec2.integer.w = w2;
                vec3.integer.x = w3;
                vec3.integer.y = w3;
                vec3.integer.z = w3;
                vec3.integer.w = w3;
                vec4.integer.x = w4;
                vec4.integer.y = w4;
                vec4.integer.z = w4;
                vec4.integer.w = w4;
                storage[i + 0] = vec1;
                if (amount >= 2)
                {
                    storage[i + 1] = vec2;
                }
                if (amount >= 3)
                {
                    storage[i + 2] = vec3;
                }
                if (amount >= 4)
                {
                    storage[i + 3] = vec4;
                }
                srcIdx++;
            }
            break;
        }
        case V2_32:
        {
            for (uint32_t i = 0; i < amount; ++i)
            {
                TwinStudio_VIFVector vec = { 0 };
                vec.integer.x = stack[i * 2 + 0].value;
                vec.integer.y = stack[i * 2 + 1].value;
                storage[i] = vec;
            }
            break;
        }
        case V2_16:
        {
            for (uint32_t i = 0; i < amount; ++i)
            {
                TwinStudio_VIFVector vec = { 0 };
                const uint32_t w1 = SEXT16(stack[i].lo, usn);
                const uint32_t w2 = SEXT16(stack[i].hi, usn);
                vec.integer.x = w1;
                vec.integer.y = w2;
                storage[i] = vec;
            }
            break;
        }
        case V2_8:
        {
            for (uint32_t i = 0; i < amount; i += 2)
            {
                TwinStudio_VIFVector vec1 = { 0 };
                TwinStudio_VIFVector vec2 = { 0 };
                const uint32_t w1 = SEXT8(stack[srcIdx].b1, usn);
                const uint32_t w2 = SEXT8(stack[srcIdx].b2, usn);
                const uint32_t w3 = SEXT8(stack[srcIdx].b3, usn);
                const uint32_t w4 = SEXT8(stack[srcIdx].b4, usn);
                vec1.integer.x = w1;
                vec1.integer.y = w2;
                vec2.integer.x = w3;
                vec2.integer.y = w4;
                storage[i] = vec1;
                if (amount >= 2)
                {
                    storage[i + 1] = vec2;
                }
                srcIdx++;
            }
            break;
        }
        case V3_32:
        {
            for (uint32_t i = 0; i < amount; ++i)
            {
                TwinStudio_VIFVector vec = { 0 };
                vec.integer.x = stack[i * 3 + 0].value;
                vec.integer.y = stack[i * 3 + 1].value;
                vec.integer.z = stack[i * 3 + 2].value;
                storage[i] = vec;
            }
            break;
        }
        case V3_16:
        {
            for (uint32_t i = 0; i < amount; ++i)
            {
                TwinStudio_VIFVector vec = { 0 };
                const uint32_t w1 = SEXT16((i % 2 == 0) ? stack[srcIdx].lo   : stack[srcIdx++].hi, usn);
                const uint32_t w2 = SEXT16((i % 2 == 0) ? stack[srcIdx++].hi : stack[srcIdx].lo,   usn);
                const uint32_t w3 = SEXT16((i % 2 == 0) ? stack[srcIdx].lo   : stack[srcIdx++].hi, usn);
                vec.integer.x = w1;
                vec.integer.y = w2;
                vec.integer.z = w3;
                storage[i] = vec;
            }
            break;
        }
        case V3_8:
        {
            for (uint32_t i = 0; i < amount; ++i)
            {
                TwinStudio_VIFVector vec = { 0 };
                const uint32_t w1 = SEXT8(
                    (i % 4 == 0) ? stack[srcIdx].b1 :
                    ((i % 4 == 1) ? stack[srcIdx++].b4 :
                    ((i % 4 == 2) ? stack[srcIdx].b3 :
                    stack[srcIdx].b2)), usn);
                const uint32_t w2 = SEXT8(
                    (i % 4 == 0) ? stack[srcIdx].b2 :
                    ((i % 4 == 1) ? stack[srcIdx].b1 :
                    ((i % 4 == 2) ? stack[srcIdx++].b4 :
                    stack[srcIdx].b3)), usn);
                const uint32_t w3 = SEXT8(
                    (i % 4 == 0) ? stack[srcIdx].b3 :
                    ((i % 4 == 1) ? stack[srcIdx].b2 :
                    ((i % 4 == 2) ? stack[srcIdx].b1 :
                    stack[srcIdx++].b4)), usn);
                vec.integer.x = w1;
                vec.integer.y = w2;
                vec.integer.z = w3;
                storage[i] = vec;
            }
            break;
        }
        case V4_32:
        {
            for (uint32_t i = 0; i < amount; ++i)
            {
                TwinStudio_VIFVector vec = { 0 };
                vec.integer.x = stack[i * 4 + 0].value;
                vec.integer.y = stack[i * 4 + 1].value;
                vec.integer.z = stack[i * 4 + 2].value;
                vec.integer.w = stack[i * 4 + 3].value;
                storage[i] = vec;
            }
            break;
        }
        case V4_16:
        {
            for (uint32_t i = 0; i < amount; ++i)
            {
                TwinStudio_VIFVector vec = { 0 };
                const uint32_t w1 = SEXT16(stack[srcIdx].lo, usn);
                const uint32_t w2 = SEXT16(stack[srcIdx++].hi, usn);
                const uint32_t w3 = SEXT16(stack[srcIdx].lo, usn);
                const uint32_t w4 = SEXT16(stack[srcIdx++].hi, usn);
                vec.integer.x = w1;
                vec.integer.y = w2;
                vec.integer.z = w3;
                vec.integer.w = w4;
                storage[i] = vec;
            }
            break;
        }
        case V4_8:
        {
            for (uint32_t i = 0; i < amount; ++i)
            {
                TwinStudio_VIFVector vec = { 0 };
                const uint32_t w1 = SEXT8(stack[i].b1, usn);
                const uint32_t w2 = SEXT8(stack[i].b2, usn);
                const uint32_t w3 = SEXT8(stack[i].b3, usn);
                const uint32_t w4 = SEXT8(stack[i].b4, usn);
                vec.integer.x = w1;
                vec.integer.y = w2;
                vec.integer.z = w3;
                vec.integer.w = w4;
                storage[i] = vec;
            }
            break;
        }
        case V4_5:
        {
            for (uint32_t i = 0; i < amount; ++i)
            {
                TwinStudio_VIFVector vec = { 0 };
                const CompactColor rgba = (i % 2 == 0) ? stack[srcIdx].c1 : stack[srcIdx++].c2;
                vec.integer.x = rgba.r << 3;
                vec.integer.y = rgba.g << 3;
                vec.integer.z = rgba.b << 3;
                vec.integer.w = rgba.a << 7;
                storage[i] = vec;
            }
            break;
        }
    }
}


static uint32_t* Pack(TwinStudio_VIFVector* vectors, uint32_t amount, TwinStudio_VIFPackFormat packFormat)
{
    uint32_t* packedVectors = NULL;
    size_t vecAmount = amount;

    arrsetcap(packedVectors, 128);
    switch (packFormat)
    {
        case V2_32:
        {
            
            for (size_t i = 0; i < vecAmount; ++i)
            {
                arrput(packedVectors, vectors[i].integer.x);
                arrput(packedVectors, vectors[i].integer.y);
            }
            break;
        }
        case V3_32:
        {
            for (size_t i = 0; i < vecAmount; ++i)
            {
                arrput(packedVectors, vectors[i].integer.x);
                arrput(packedVectors, vectors[i].integer.y);
                arrput(packedVectors, vectors[i].integer.z);
            }
            break;
        }
        case V4_16:
        {
            for (size_t i = 0; i < vecAmount; ++i)
            {
                uint32_t result = vectors[i].integer.x & 0xFFFF;
                result |= (vectors[i].integer.y & 0xFFFF) << 16;
                arrput(packedVectors, result);
                result = vectors[i].integer.z & 0xFFFF;
                result |= (vectors[i].integer.w & 0xFFFF) << 16;
                arrput(packedVectors, result);
            }
            break;
        }
        case V4_32:
        {
            for (size_t i = 0; i < vecAmount; ++i)
            {
                arrput(packedVectors, vectors[i].integer.x);
                arrput(packedVectors, vectors[i].integer.y);
                arrput(packedVectors, vectors[i].integer.z);
                arrput(packedVectors, vectors[i].integer.w);
            }
            break;
        }
        case V4_8:
        {
            for (size_t i = 0; i < vecAmount; ++i)
            {
                uint32_t result = vectors[i].integer.x & 0xFF;
                result |= (vectors[i].integer.y & 0xFF) << 8;
                result |= (vectors[i].integer.z & 0xFF) << 16;
                result |= (vectors[i].integer.w & 0xFF) << 24;
                arrput(packedVectors, result);
            }
            break;
        }
        default:
            break;
    }

    return packedVectors;
}


TwinStudio_VIFOutput TwinStudio_VIFInterpretData(void* data, size_t dataSize, TwinStudio_Arena* arena)
{
    TwinStudio_BinarySerializer* vifReader = TwinStudio_BinSerializerAllocate(data, TwinStudio_BinarySerializerModeRead, dataSize, false);
    TwinStudio_VIFOutput output = TwinStudio_VIFInterpret(vifReader, arena);
    TwinStudio_BinSerializerFree(vifReader);
    return output;
}


TwinStudio_VIFOutput TwinStudio_VIFInterpret(TwinStudio_BinarySerializer* reader, TwinStudio_Arena* arena)
{
    TwinStudio_VIFOutput output = { 0 };
    TwinStudio_VIFInterpreter interpreter = { 0 };

    TwinStudio_DmaTag dmaTag = { 0 };
    TwinStudio_BinReadStructDirect(reader, &dmaTag, sizeof(TwinStudio_DmaTag));
    int32_t dmaTagUsed = 0;

    while (TwinStudio_BinGetStreamPosition(reader) < TwinStudio_BinGetStreamLength(reader))
    {
        TwinStudio_VIFInstruction instruction;
        if (dmaTagUsed >= 2)
        {
            TwinStudio_BinReadStructDirect(reader, &instruction, sizeof(TwinStudio_VIFInstruction));
        }
        else
        {
            instruction.fullInstruction = (dmaTag.extra >> (32 * dmaTagUsed)) & 0xFFFFFFFF;
            dmaTagUsed++;
        }
        if ((instruction.cmd & UNPACK) == UNPACK)
        {
            uint16_t addr = instruction.unpack.addr;
            bool usn = instruction.unpack.usn;
            uint8_t wl = (interpreter.vifn_cycle >> 8) & 0xFF;
            uint8_t cl = (interpreter.vifn_cycle >> 0) & 0xFF;
            uint32_t dimensions = instruction.unpck.vn + 1;
            uint16_t amount = instruction.num;
            if (amount == 0)
            {
                amount = 256;
            }
            bool isFill = wl > cl;

            uint32_t packet_length = 0;
            if (!isFill)
            {
                uint32_t a = 32 >> instruction.unpck.vl;
                uint32_t b = dimensions;
                float c = a * b * amount;
                float d = c / 32.0f;
                float e = ceilf(d);
                uint32_t f = (uint32_t)e;
                packet_length = 1 + f;
            }
            else
            {
                uint32_t n = (cl * (amount / wl) + (amount % wl)) > cl ? cl : (amount % wl);
                uint32_t a = 32 >> instruction.unpck.vl;
                uint32_t b = dimensions;
                float c = a * b * n;
                float d = c / 32.0f;
                float e = ceilf(d);
                uint32_t f = (uint32_t)e;
                packet_length = 1 + f;
            }

            CompactVector tmpStack[400];
            assert((packet_length - 1) < 400);
            for (uint8_t i = 0; i < packet_length - 1; ++i)
            {
                tmpStack[i].value = TwinStudio_BinReadUInt32(reader);
            }

            TwinStudio_VIFVector tmpStorage[100];
            assert(amount < 100);
            Unpack(tmpStack, tmpStorage, &interpreter, reader, (instruction.unpck.vl | (instruction.unpck.vn << 2)), amount, usn);
            for (uint8_t i = 0; i < amount; ++i)
            {
                switch (addr)
                {
                    case 0: // Stores vector amount
                        output.metaVectors[0] = tmpStorage[0];
                        arrput(output.groupSizes, (tmpStorage[0].integer.x & 0xFF));
                        break;
                    case 1: // In theory stores amount of present fields
                        output.metaVectors[1] = tmpStorage[0];
                        break;
                    case 2: // For Skins And Blend Skins the compressed data scaling vector
                        output.scaleVector = tmpStorage[0];
                        break;
                    case 3:
                        arrput(output.vertexes, tmpStorage[i]);
                        break;
                    case 4:
                        arrput(output.uvColors, tmpStorage[i]);
                        break;
                    case 5:
                        if (output.scaleVector.integer.x == 0 && output.scaleVector.integer.y == 0)
                        {
                            arrput(output.normals, tmpStorage[i]);
                        }
                        else
                        {
                            arrput(output.jointWeights, tmpStorage[i]);
                        }
                        break;
                    case 6:
                        arrput(output.emits, tmpStorage[i]);
                        break;
                    case 7:
                        arrput(output.blendFaceOffsets, tmpStorage[i]);
                        break;
                }
            }
        }
        else
        {
            switch (instruction.cmd)
            {
                case NOP:
                    continue;
                case STCYCL:
                    interpreter.vifn_cycle = instruction.immediate;
                    break;
                case OFFSET:
                    interpreter.vifn_ofst = instruction.offset.offset;
                    break;
                case BASE:
                    interpreter.vifn_base = instruction.base.base;
                    break;
                case ITOP:
                    interpreter.vifn_itop = instruction.itop.addr;
                    break;
                case STMOD:
                    interpreter.vifn_mode = instruction.stmod.mode;
                    break;
                case MSKPATH3:
                    break;
                case MARK:
                    interpreter.vifn_mark = instruction.mark.mark;
                    break;
                case FLUSHE:
                case FLUSH:
                case FLUSHA:
                case MSCAL:
                case MSCNT:
                case MSCALF:
                    break;
                case STMASK:
                    interpreter.vifn_mask = TwinStudio_BinReadUInt32(reader);
                    break;
                case STROW:
                    interpreter.vifn_r[0] = TwinStudio_BinReadUInt32(reader);
                    interpreter.vifn_r[1] = TwinStudio_BinReadUInt32(reader);
                    interpreter.vifn_r[2] = TwinStudio_BinReadUInt32(reader);
                    interpreter.vifn_r[3] = TwinStudio_BinReadUInt32(reader);
                    break;
                case STCOL:
                    interpreter.vifn_c[0] = TwinStudio_BinReadUInt32(reader);
                    interpreter.vifn_c[1] = TwinStudio_BinReadUInt32(reader);
                    interpreter.vifn_c[2] = TwinStudio_BinReadUInt32(reader);
                    interpreter.vifn_c[3] = TwinStudio_BinReadUInt32(reader);
                    break;
                case MPG:
                    break;
                case DIRECT:
                {
                    TwinStudio_ResultingGifTag tag = { 0 };
                    arrsetcap(output.gifTags, 64);
                    while (tag.gifTag.eop != 1)
                    {
                        tag = TwinStudio_GifTagRead(reader, arena);
                        arrput(output.gifTags, tag);
                    }
                    break;
                }
                case DIRECTHL:
                default:
                    break;
            }
        }
    }

    return output;
}


static uint32_t GetVIFInstructionLength(const TwinStudio_VIFInstruction* const instruction)
{
    uint32_t resultLength = 0;
    if ((instruction->cmd & UNPACK) != 0)
    {
        uint8_t amount = instruction->num;
        uint32_t a = 32 >> instruction->unpck.vl;
        uint32_t b = instruction->unpck.vn + 1;
        float c = a * b * amount;
        float d = c / 32.0f;
        float e = ceilf(d);
        uint32_t f = (uint32_t)e;
        resultLength = (1 + f) * 4;
    }
    else
    {
        switch (instruction->cmd) {
            case STMASK:
                resultLength = 8;
                break;
            case STROW:
            case STCOL:
                resultLength = 20;
                break;
            default:
                resultLength = 4;
                break;
        }
    }

    return resultLength;
}


TwinStudio_VIFCompilerOutput TwinStudio_VIFCompile(TwinStudio_VIFOutput* input)
{
    TwinStudio_VIFCompilerOutput result = { 0 };
    const uint32_t bufferSize = 1024 * 1024;
    uint8_t* buffer = NULL;
    arrsetcap(buffer, bufferSize);
    result.vifByteCode = buffer;
    TwinStudio_BinarySerializer* writer = TwinStudio_BinSerializerAllocate(buffer, TwinStudio_BinarySerializerModeWrite, bufferSize, false);

    if (input->blendFaceOffsets != NULL)
    {
        uint8_t* faceBuffer = NULL;
        arrsetcap(faceBuffer, bufferSize);
        result.packedBlendFaces = faceBuffer;
        TwinStudio_BinarySerializer* faceWriter = TwinStudio_BinSerializerAllocate(faceBuffer, TwinStudio_BinarySerializerModeWrite, bufferSize, false);
        uint32_t* packedBlends = Pack(input->blendFaceOffsets, arrlen(input->blendFaceOffsets), V4_8);
        uint32_t packedAmount = arrlen(packedBlends);
        TwinStudio_BinWriteAny(faceWriter, packedBlends, packedAmount * 4);
        arrfree(packedBlends);

        uint32_t spaceNeeded = packedAmount * 4;
        while (spaceNeeded % 0x10 != 0)
        {
            TwinStudio_BinWriteUInt32(faceWriter, 0U);
            spaceNeeded += 4;
        }

        TwinStudio_BinSerializerFree(faceWriter);
    }

    TwinStudio_DmaTag dmaTag = { .id = TS_DIT_RET, .extra = 0x00000000070000fa, .qwc = 0 }; // QWC calculated at the end
    size_t dmaTagStreamPos = TwinStudio_BinGetStreamPosition(writer);
    TwinStudio_BinWriteAny(writer, &dmaTag, sizeof(TwinStudio_DmaTag));

    const TwinStudio_VIFInstruction nop = { .cmd = NOP };
    const uint32_t groupAmounts = arrlen(input->groupSizes);
    if (groupAmounts == 0)
    {
        return result;
    }

    uint32_t totalSpaceNeeded = 0;
    uint32_t vecOffset = 0;
    const bool isSkin = (input->scaleVector.floating.x > 0.0f && input->scaleVector.floating.y > 0.0f);
    for (uint32_t i = 0; i < groupAmounts; ++i)
    {
        const uint32_t groupSize = input->groupSizes[i];

        TwinStudio_VIFInstruction modelDescInstruction = { 0 };
        modelDescInstruction.cmd = UNPACK | V4_32;
        modelDescInstruction.unpack.addr = 0;
        modelDescInstruction.unpack.flg = true;
        modelDescInstruction.num = 1;
        TwinStudio_BinWriteAny(writer, &modelDescInstruction, sizeof(TwinStudio_VIFInstruction));

        // A GIF tag describing what GS registers to set when sending the model data to GS
        TwinStudio_VIFVector modelDescVec = { 0 };
        modelDescVec.integer.x = 0x8000 | groupSize; // Set End of packet flag and the amount of loops
        modelDescVec.integer.y = 0x30024000; // Set primitive as triangle strip in PACKED mode with 3 descriptors
        modelDescVec.integer.z = 0x512; // Use REGS ST, RGBAQ and XYZ2 (in that exact order) as GS outputs
        modelDescVec.integer.w = 0;

        uint32_t* packed = Pack(&modelDescVec, 1, V4_32);
        TwinStudio_BinWriteAny(writer, packed, arrlen(packed) * 4);
        arrfree(packed);
        totalSpaceNeeded += GetVIFInstructionLength(&modelDescInstruction);

        TwinStudio_VIFInstruction setCycleInstruction = { 0 };
        setCycleInstruction.cmd = STCYCL;
        setCycleInstruction.stcycl.cl = 1;
        setCycleInstruction.stcycl.wl = 1;
        TwinStudio_BinWriteAny(writer, &setCycleInstruction, sizeof(TwinStudio_VIFInstruction));
        totalSpaceNeeded += GetVIFInstructionLength(&setCycleInstruction);

        TwinStudio_VIFInstruction metaVecInstruction = { 0 };
        metaVecInstruction.cmd = UNPACK | V2_32;
        metaVecInstruction.unpack.addr = 1;
        metaVecInstruction.unpack.flg = true;
        metaVecInstruction.num = 1;
        TwinStudio_BinWriteAny(writer, &metaVecInstruction, sizeof(TwinStudio_VIFInstruction));

        TwinStudio_VIFVector metaVec = { 0 };
        metaVec.integer.x = groupSize * 4;
        metaVec.integer.y = 0x8000 | groupSize;
        packed = Pack(&metaVec, 1, V2_32);
        TwinStudio_BinWriteAny(writer, packed, arrlen(packed) * 4);
        arrfree(packed);
        totalSpaceNeeded += GetVIFInstructionLength(&metaVecInstruction);

        if (isSkin)
        {
            TwinStudio_VIFInstruction scaleVecInstruction = { 0 };
            scaleVecInstruction.cmd = UNPACK | V2_32;
            scaleVecInstruction.num = 1;
            scaleVecInstruction.unpack.addr = 2;
            scaleVecInstruction.unpack.flg = true;
            TwinStudio_BinWriteAny(writer, &scaleVecInstruction, sizeof(TwinStudio_VIFInstruction));
            packed = Pack(&input->scaleVector, 1, V2_32);
            TwinStudio_BinWriteAny(writer, packed, arrlen(packed) * 4);
            arrfree(packed);
            totalSpaceNeeded += GetVIFInstructionLength(&scaleVecInstruction);

            TwinStudio_VIFInstruction turnOnOffsetInstruction = { 0 };
            turnOnOffsetInstruction.cmd = STMOD;
            turnOnOffsetInstruction.stmod.mode = 1;
            TwinStudio_BinWriteAny(writer, &turnOnOffsetInstruction, sizeof(TwinStudio_VIFInstruction));
            totalSpaceNeeded += GetVIFInstructionLength(&turnOnOffsetInstruction);

            TwinStudio_VIFInstruction setRowInstruction = { 0 };
            setRowInstruction.cmd = STROW;
            TwinStudio_BinWriteAny(writer, &setRowInstruction, sizeof(TwinStudio_VIFInstruction));
            TwinStudio_BinWriteUInt32(writer, 0);
            TwinStudio_BinWriteUInt32(writer, 0);
            TwinStudio_BinWriteUInt32(writer, 0);
            TwinStudio_BinWriteUInt32(writer, 0);
            totalSpaceNeeded += GetVIFInstructionLength(&setRowInstruction);
        }

        setCycleInstruction = (TwinStudio_VIFInstruction) { 0 };
        setCycleInstruction.cmd = STCYCL;
        setCycleInstruction.stcycl.cl = 4;
        setCycleInstruction.stcycl.wl = 1;
        TwinStudio_BinWriteAny(writer, &setCycleInstruction, sizeof(TwinStudio_VIFInstruction));
        totalSpaceNeeded += GetVIFInstructionLength(&setCycleInstruction);

        if (!isSkin) // Rigid models
        {
            TwinStudio_VIFInstruction vertexInstruction = { 0 };
            vertexInstruction.cmd = UNPACK | V3_32;
            vertexInstruction.unpack.addr = 3;
            vertexInstruction.unpack.flg = true;
            vertexInstruction.num = groupSize;
            TwinStudio_BinWriteAny(writer, &vertexInstruction, sizeof(TwinStudio_VIFInstruction));

            packed = Pack(input->vertexes + vecOffset, groupSize, V3_32);
            TwinStudio_BinWriteAny(writer, packed, arrlen(packed) * 4);
            arrfree(packed);
            totalSpaceNeeded += GetVIFInstructionLength(&vertexInstruction);

            TwinStudio_VIFInstruction uvColorsInstruction = { 0 };
            uvColorsInstruction.cmd = UNPACK | V4_32;
            uvColorsInstruction.unpack.addr = 4;
            uvColorsInstruction.unpack.flg = true;
            uvColorsInstruction.num = groupSize;
            TwinStudio_BinWriteAny(writer, &uvColorsInstruction, sizeof(TwinStudio_VIFInstruction));

            packed = Pack(input->uvColors + vecOffset, groupSize, V4_32);
            TwinStudio_BinWriteAny(writer, packed, arrlen(packed) * 4);
            arrfree(packed);
            totalSpaceNeeded += GetVIFInstructionLength(&uvColorsInstruction);

            if (input->normals)
            {
                TwinStudio_VIFInstruction normalsInstruction = { 0 };
                normalsInstruction.cmd = UNPACK | V3_32;
                normalsInstruction.num = groupSize;
                normalsInstruction.unpack.addr = 5;
                normalsInstruction.unpack.flg = true;
                TwinStudio_BinWriteAny(writer, &normalsInstruction, sizeof(TwinStudio_VIFInstruction));

                packed = Pack(input->normals + vecOffset, groupSize, V3_32);
                TwinStudio_BinWriteAny(writer, packed, arrlen(packed) * 4);
                arrfree(packed);
                totalSpaceNeeded += GetVIFInstructionLength(&normalsInstruction);
            }

            if (input->emits)
            {
                TwinStudio_VIFInstruction emitsInstruction = { 0 };
                emitsInstruction.cmd = UNPACK | V4_8;
                emitsInstruction.num = groupSize;
                emitsInstruction.unpack.addr = 6;
                emitsInstruction.unpack.flg = true;
                TwinStudio_BinWriteAny(writer, &emitsInstruction, sizeof(TwinStudio_VIFInstruction));

                packed = Pack(input->emits + vecOffset, groupSize, V4_8);
                TwinStudio_BinWriteAny(writer, packed, arrlen(packed) * 4);
                arrfree(packed);
                totalSpaceNeeded += GetVIFInstructionLength(&emitsInstruction);
            }
        }
        else // Skins and blend skins
        {
            for (uint32_t j = vecOffset; j < vecOffset + groupSize; ++j)
            {
                TwinStudio_VIFVector* position = input->vertexes + j;
                position->integer.x = (int16_t)roundf(position->floating.x / input->scaleVector.floating.x);
                position->integer.y = (int16_t)roundf(position->floating.y / input->scaleVector.floating.x);
                position->integer.z = (int16_t)roundf(position->floating.z / input->scaleVector.floating.x);
                position->integer.w = (int16_t)roundf(position->floating.w / input->scaleVector.floating.x);
                TwinStudio_VIFVector* uv = input->uvColors + j;
                uv->integer.x = (int16_t)roundf(uv->floating.x / input->scaleVector.floating.y);
                uv->integer.y = (int16_t)roundf(uv->floating.y / input->scaleVector.floating.y);
                uv->integer.z = (int16_t)roundf(uv->floating.z / input->scaleVector.floating.y);
                uv->integer.w = (int16_t)roundf(uv->floating.w / input->scaleVector.floating.y);
            }

            TwinStudio_VIFInstruction vertexInstruction = { 0 };
            vertexInstruction.num = groupSize;
            vertexInstruction.cmd = UNPACK | V4_16;
            vertexInstruction.unpack.flg = true;
            vertexInstruction.unpack.addr = 3;
            TwinStudio_BinWriteAny(writer, &vertexInstruction, sizeof(TwinStudio_VIFInstruction));

            packed = Pack(input->vertexes + vecOffset, groupSize, V4_16);
            TwinStudio_BinWriteAny(writer, packed, arrlen(packed) * 4);
            arrfree(packed);
            totalSpaceNeeded += GetVIFInstructionLength(&vertexInstruction);

            TwinStudio_VIFInstruction setRowInstruction = { 0 };
            setRowInstruction.cmd = STROW;
            TwinStudio_BinWriteAny(writer, &setRowInstruction, sizeof(TwinStudio_VIFInstruction));
            TwinStudio_BinWriteUInt32(writer, 0);
            TwinStudio_BinWriteUInt32(writer, 0);
            TwinStudio_BinWriteUInt32(writer, 0);
            TwinStudio_BinWriteUInt32(writer, 0);
            totalSpaceNeeded += GetVIFInstructionLength(&setRowInstruction);

            TwinStudio_VIFInstruction uvInstruction = { 0 };
            uvInstruction.num = groupSize;
            uvInstruction.cmd = UNPACK | V4_16;
            uvInstruction.unpack.addr = 4;
            uvInstruction.unpack.flg = true;
            TwinStudio_BinWriteAny(writer, &uvInstruction, sizeof(TwinStudio_VIFInstruction));

            packed = Pack(input->uvColors + vecOffset, groupSize, V4_16);
            TwinStudio_BinWriteAny(writer, packed, arrlen(packed) * 4);
            arrfree(packed);
            totalSpaceNeeded += GetVIFInstructionLength(&uvInstruction);

            TwinStudio_VIFInstruction turnOffOffsetInstruction = { 0 };
            turnOffOffsetInstruction.cmd = STMOD;
            turnOffOffsetInstruction.stmod.mode = 0;
            TwinStudio_BinWriteAny(writer, &turnOffOffsetInstruction, sizeof(TwinStudio_VIFInstruction));
            totalSpaceNeeded += GetVIFInstructionLength(&turnOffOffsetInstruction);

            TwinStudio_VIFInstruction colorInstruction = { 0 };
            colorInstruction.cmd = UNPACK | V4_8;
            colorInstruction.num = groupSize;
            colorInstruction.unpack.addr = 5;
            colorInstruction.unpack.flg = true;
            TwinStudio_BinWriteAny(writer, &colorInstruction, sizeof(TwinStudio_VIFInstruction));

            packed = Pack(input->emits + vecOffset, groupSize, V4_8);
            TwinStudio_BinWriteAny(writer, packed, arrlen(packed) * 4);
            arrfree(packed);
            totalSpaceNeeded += GetVIFInstructionLength(&colorInstruction);

            TwinStudio_VIFInstruction jointInstruction = { 0 };
            jointInstruction.num = groupSize;
            jointInstruction.cmd = UNPACK | V4_32;
            jointInstruction.unpack.addr = 6;
            jointInstruction.unpack.flg = true;
            TwinStudio_BinWriteAny(writer, &jointInstruction, sizeof(TwinStudio_VIFInstruction));

            packed = Pack(input->jointWeights + vecOffset, groupSize, V4_32);
            TwinStudio_BinWriteAny(writer, packed, arrlen(packed) * 4);
            arrfree(packed);
            totalSpaceNeeded += GetVIFInstructionLength(&jointInstruction);
        }

        if (input->blendFaceOffsets == NULL)
        {
            TwinStudio_VIFInstruction mscalInstruction = { 0 };
            mscalInstruction.cmd = MSCAL;
            TwinStudio_BinWriteAny(writer, &mscalInstruction, sizeof(TwinStudio_VIFInstruction));
            totalSpaceNeeded += GetVIFInstructionLength(&mscalInstruction);

            setCycleInstruction = (TwinStudio_VIFInstruction) { 0 };
            setCycleInstruction.cmd = STCYCL;
            setCycleInstruction.stcycl.cl = 1;
            setCycleInstruction.stcycl.wl = 1;
            TwinStudio_BinWriteAny(writer, &setCycleInstruction, sizeof(TwinStudio_VIFInstruction));
            totalSpaceNeeded += GetVIFInstructionLength(&setCycleInstruction);
        }

        vecOffset += groupSize;
    }

    while ((totalSpaceNeeded % 0x10) != 0)
    {
        TwinStudio_BinWriteAny(writer, &nop, sizeof(TwinStudio_VIFInstruction));
        totalSpaceNeeded += GetVIFInstructionLength(&nop);
    }

    dmaTag.qwc = ((TwinStudio_BinGetStreamPosition(writer) + 1) / 0x10 - 1);
    TwinStudio_BinSerializerSetPosition(writer, dmaTagStreamPos);
    TwinStudio_BinWriteAny(writer, &dmaTag, sizeof(TwinStudio_DmaTag));
    TwinStudio_BinSerializerFree(writer);

    return result;
}