#include "audio_serializers.h"
#include "retail/auto_struct_mb_archive.h"
#include "retail/auto_struct_mh_archive.h"
#include "serialization/binary_serializer.h"
#include "audio/adpcm.h"


static int32_t GetPcm(void *priv, double *out, int32_t len)
{
    int32_t i;
	TwinStudio_PcmBuffer *pcm = priv;

	for (i = 0; i < len; i++)
	{
		if (pcm->position + i >= pcm->sampleCount)
        {
			break;
        }

		out[i] = pcm->sample[((pcm->position + i) * pcm->channelCount) + pcm->channel];
	}
	pcm->position += i;

	return i;
}


static int32_t PutAdpcm(void *priv, void *data, int32_t len)
{
    TwinStudio_BinarySerializer* serializer = priv;
    size_t curPostion = TwinStudio_BinGetStreamPosition(serializer);
    TwinStudio_BinWriteAny(serializer, data, len);
    return TwinStudio_BinGetStreamPosition(serializer) - curPostion;
}


void TwinStudio_WaveBinSerialize(TwinStudio_Wave* source, TwinStudio_BinarySerializer* serializer, TwinStudio_Arena* arena, size_t size, void* userData)
{
    
    TwinRes_MbRecord* record = userData;
    if (record->header.type == TwinRes_MRT_Null)
    {
        return;
    }

    TwinStudio_PcmBuffer pcmBuffer;
    pcmBuffer.channelCount = source->channels;

    TwinStudio_AdpcmSetup* setups[2];

    for (uint32_t i = 0; i < pcmBuffer.channelCount; ++i)
    {
        setups[i] = TwinStudio_AdpcmCreate(arena, GetPcm, &pcmBuffer, PutAdpcm, serializer, source->loopPosition);
    }

    if (pcmBuffer.channelCount > 2)
    {
        setups[0]->pad = 1;
        setups[0]->pad = 1;
    }

    int32_t bpc = record->header.interleave;
    uint32_t dataOffset = 0;
    pcmBuffer.sample = source->data;
    do
    {
        uint32_t r = 2 * pcmBuffer.channelCount * bpc * 28;
        if (dataOffset + r >= source->dataSize)
        {
            r = (source->dataSize - dataOffset);
        }
        if (r == 0)
        {
            break;
        }

        dataOffset += r;
        pcmBuffer.sampleCount = r / (2 * pcmBuffer.channelCount);

        uint32_t i;
        for (i = 0; i < pcmBuffer.channelCount; ++i)
        {
            pcmBuffer.position = 0;
            pcmBuffer.channel = i;
            if (TwinStudio_AdpcmEncode(setups[i], bpc) != bpc)
            {
                break;
            }
        }

        if (i != pcmBuffer.channelCount)
        {
            break;
        }

        pcmBuffer.sample = (void*)(((uint8_t*)source->data) + dataOffset);
    } while (true);
}


void TwinStudio_WaveBinDeserialize(TwinStudio_Wave* target, TwinStudio_BinarySerializer* deserializer, TwinStudio_Arena* arena, size_t size, void* userData)
{
    TwinRes_MbRecord* record = userData;
    if (record->header.type == TwinRes_MRT_Null)
    {
        return;
    }

    uint32_t interleave = record->header.type != TwinRes_MRT_Stereo ? 0 : record->header.interleave;
    void* audioData = TwinStudio_BinReadBlob(deserializer, arena, size);
    TwinStudio_AdpcmDecodeResult result = TwinStudio_AdpcmDecode(arena, audioData, size, interleave);
    target->data = result.pcmData;
    target->dataSize = result.pcmDataSize;
    target->loopPosition = result.loopPosition;
    if (record->header.type == TwinRes_MRT_Stereo)
    {
        target->samplerate = record->header.sampleRate;
    }
    else
    {
        target->samplerate = record->sampleRate;
    }
    target->channels = record->header.type == TwinRes_MRT_Stereo ? 2 : 1;
}
