#include "gif_tag.h"
#include "memory/memory.h"
#include "serialization/binary_serializer.h"


TwinStudio_GifAddressOutput ProcessGsInput(TwinStudio_BinarySerializer* serializer, TwinStudio_GIFModeEnum mode, TwinStudio_GsRegsEnum reg)
{
    TwinStudio_GsRegInput input;
    TwinStudio_GifAddressOutput output;

    if (mode != DISABLE)
    {
        const size_t readSize = mode == REGLIST ? sizeof(TwinStudio_GsRegInput) / 2 : sizeof(TwinStudio_GsRegInput);
        TwinStudio_BinReadStructDirect(serializer, &input, readSize);
    }

    switch (mode) {
        case PACKED:
            output.gsOuputsLength = 1;
            switch (reg) {
                case TS_GS_REG_RGBAQ:
                    output.gsOutput[0].rgbaq.r = input.rgbaq.r;
                    output.gsOutput[0].rgbaq.g = input.rgbaq.g;
                    output.gsOutput[0].rgbaq.b = input.rgbaq.b;
                    output.gsOutput[0].rgbaq.a = input.rgbaq.a;
                    break;
                case TS_GS_REG_ST:
                    output.gsOutput[0].st.s = input.st.s;
                    output.gsOutput[0].st.t = input.st.t;
                    // Q parameter deliberately ignored
                    break;
                case TS_GS_REG_UV:
                    output.gsOutput[0].uv.u = input.uv.u;
                    output.gsOutput[0].uv.v = input.uv.v;
                    break;
                case TS_GS_REG_ApD:
                    output.gsOutput[0].apd.data = input.apd.data;
                    output.gsRegAddress = input.apd.address;
                    break;
                default:
                    break;
            }
        case REGLIST:
            output.gsOuputsLength = 1;
            output.gsOutput[0].apd.data = input.raw.low;
            break;
        case IMAGE:
            output.gsOuputsLength = 2;
            output.gsOutput[0].apd.data = input.raw.low;
            output.gsOutput[1].apd.data = input.raw.high;
            break;
        case DISABLE:
            break;
    }


    return output;
}


TwinStudio_ResultingGifTag TwinStudio_GifTagRead(TwinStudio_BinarySerializer* serializer, TwinStudio_Arena* arena)
{
    TwinStudio_GifTag gifTag;
    TwinStudio_BinReadStructDirect(serializer, &gifTag, sizeof(TwinStudio_GifTag));
    uint8_t nreg = gifTag.nreg;
    if (nreg == 0)
    {
        nreg = 16;
    }

    TwinStudio_ResultingGifTag result = { 0 };
    result.gifTag = gifTag;

    switch (gifTag.flg) {
        case PACKED:
        case REGLIST:
            result.outputsLength = gifTag.nloop * nreg;
            break;
        case IMAGE:
            result.outputsLength = gifTag.nloop;
            break;
        default:
            break;
    }

    if (gifTag.pre == 1)
    {
        result.outputsLength++;
    }

    if (result.outputsLength > 0)
    {
        result.outputs = TwinStudio_ArenaAlloc(arena, (sizeof *result.outputs) * result.outputsLength);
    }

    int32_t outputIndex;
    if (gifTag.pre == 1)
    {
        TwinStudio_GsRegOutput* output = &result.outputs[0].gsOutput[0];
        output->prim.prim = gifTag.prim;
        outputIndex = 1;
    }

    int32_t i, j;
    switch (gifTag.flg) {
        case PACKED:
        case REGLIST:
            for (i = 0; i < gifTag.nloop; ++i)
            {
                for (j = 0; j < nreg / 2; ++j)
                {
                    result.outputs[outputIndex++] = ProcessGsInput(serializer, gifTag.flg, gifTag.packedRegs[j].reg0);
                    result.outputs[outputIndex++] = ProcessGsInput(serializer, gifTag.flg, gifTag.packedRegs[j].reg1);
                }
                if ((nreg & 1) == 1)
                {
                    result.outputs[outputIndex++] = ProcessGsInput(serializer, gifTag.flg, gifTag.packedRegs[j].reg0);
                }
            }
            break;
        case IMAGE:
            for (i = 0; i < gifTag.nloop; ++i)
            {
                result.outputs[outputIndex++] = ProcessGsInput(serializer, gifTag.flg, TS_GS_REG_HWREG);
            }
            break;
        default:
            break;
    }

    return result;
}


void TwinStudio_GifTagWrite(TwinStudio_BinarySerializer* writer, TwinStudio_ResultingGifTagInput gifTag)
{
    TwinStudio_BinWriteAny(writer, &gifTag.gifTag, sizeof(TwinStudio_GifTag));

    int32_t i;
    switch (gifTag.gifTag.flg) {
        case PACKED:
        case REGLIST:
        case IMAGE:
            for (i = 0; i < gifTag.inputsLength; ++i)
            {
                TwinStudio_BinWriteAny(writer, gifTag.inputs + i, sizeof(TwinStudio_GsRegInput));
            }
            break;
        case DISABLE:
            break;
    }
}