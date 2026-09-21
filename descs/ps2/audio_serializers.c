#include "audio_serializers.h"
#include "retail/auto_struct_mb_archive.h"
#include "retail/auto_struct_mh_archive.h"
#include "serialization/binary_serializer.h"
#include "audio/adpcm.h"
#include <stdio.h>
#include <string.h>


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
    size_t curPosition = TwinStudio_BinGetStreamPosition(serializer);
    TwinStudio_BinWriteAny(serializer, data, len);
    return TwinStudio_BinGetStreamPosition(serializer) - curPosition;
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

    if (pcmBuffer.channelCount > 1)
    {
        setups[0]->pad = 1;
        setups[1]->pad = 1;
    }

    int32_t bpc = record->interleave;

    // When a chunk runs out of real samples partway through, AdpcmEncode
    // pads it out with blocks flagged LOOP_START|LOOP|LOOP_END - a
    // deliberate "loop to this silent block forever" terminator, correct for
    // a one-shot sound with no loop point. Retail music never needed that:
    // its source PCM already divides evenly into whole chunks, so it never
    // emits that filler, and the hardware/game just restarts the track from
    // byte 0 once playback runs off the end of the DMA'd size - same as it
    // does with no loop marker embedded anywhere at all. An imported
    // track's PCM almost never divides evenly, so without this its last
    // chunk gets that self-loop-into-silence filler baked in and the track
    // never restarts - it plays once and goes quiet. Padding just that
    // trailing chunk with silence up to a full chunk keeps it "full" so the
    // filler path never triggers, matching retail's own layout.
    //
    // Mono's `bpc` isn't a fixed interleave granularity like stereo's - it's
    // sized to swallow the whole track in a single AdpcmEncode call, so
    // treating it like a chunk to pad up to would balloon a mono track with
    // silence instead of trimming a few blocks off it. Mono has no channel
    // to desync against and no interleave to stay aligned to, so it doesn't
    // need this in the first place.
    uint32_t chunkBytes = 2u * pcmBuffer.channelCount * (uint32_t)bpc * 28u;
    bool padTailChunk = pcmBuffer.channelCount > 1 && chunkBytes > 0;

    uint32_t dataOffset = 0;
    pcmBuffer.sample = source->data;
    do
    {
        uint32_t remaining = source->dataSize - dataOffset;
        uint32_t r = chunkBytes;
        if (r >= remaining)
        {
            r = remaining;
        }
        if (r == 0)
        {
            break;
        }

        void *chunkSample = (uint8_t *)source->data + dataOffset;
        uint32_t sampleBytes = r;
        if (padTailChunk && r < chunkBytes)
        {
            // Only the trailing partial chunk needs a copy, and only up to
            // one chunk's worth of silence - not the whole track.
            uint8_t *padded = TwinStudio_ArenaAlloc(arena, chunkBytes);
            memcpy(padded, chunkSample, r);
            memset(padded + r, 0, chunkBytes - r);
            chunkSample = padded;
            sampleBytes = chunkBytes;
        }

        dataOffset += r;
        pcmBuffer.sample = chunkSample;
        pcmBuffer.sampleCount = sampleBytes / (2 * pcmBuffer.channelCount);

        // Every channel gets its own AdpcmEncode call for this chunk, even
        // once one of them runs short - each pads and terminates itself
        // independently. Bailing out of this loop early on a short return
        // used to skip the remaining channels' final macro-block entirely,
        // leaving the archive's block-interleave addressing pointing past
        // where that data should be for the rest of the stream.
        bool anyShort = false;
        for (uint32_t i = 0; i < pcmBuffer.channelCount; ++i)
        {
            pcmBuffer.position = 0;
            pcmBuffer.channel = i;
            int returned = TwinStudio_AdpcmEncode(setups[i], bpc);
            if (returned != bpc)
            {
                anyShort = true;
            }
        }

        if (anyShort)
        {
            break;
        }

    } while (true);
}


void TwinStudio_WaveBinDeserialize(TwinStudio_DeserializationContext* ctx, TwinStudio_Wave* target, TwinStudio_BinarySerializer* deserializer, TwinStudio_Arena* arena, size_t size, void* userData)
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
