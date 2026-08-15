/*
# _____     ___ ____     ___ ____
#  ____|   |    ____|   |        | |____|
# |     ___|   |____ ___|    ____| |    \    PS2DEV Open Source Project.
#-----------------------------------------------------------------------
# Copyright 2005, James Lee (jbit<at>jbit<dot>net)
# Licenced under Academic Free License version 2.0
# Review ps2sdk README & LICENSE files for further details.
*/



/*
	Based on:
	PSX VAG-Packer, hacked by bITmASTER@bigfoot.com, hacked by jbit :)
	jbit's note:
*/

#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "adpcm.h"
#include "defines/defines.h"
#include "memory/memory.h"
#include "serialization/binary_serializer.h"

#define ADPCM_LOOP_START    4  /* Set on first block of looped data */
#define ADPCM_LOOP          2  /* Set on all blocks (?that are inside the loop?) */
#define ADPCM_LOOP_END      1  /* Set on last block to loop */
#define ADPCM_FILE_END      (ADPCM_LOOP_START | ADPCM_LOOP | ADPCM_LOOP_END)


typedef TS_COMPACT_STRUCT
{
	uint8_t shift:      4;
	uint8_t predict:    4;
	uint8_t  flags;
	uint8_t  sample[14]; /* 4bits each */
} AdpcmBlock;

static void find_predict(TwinStudio_AdpcmSetup *set, AdpcmBlock *adpcm, double *samples);
static void pack(TwinStudio_AdpcmSetup *set, AdpcmBlock *adpcm, const double *samples);


TwinStudio_AdpcmSetup *TwinStudio_AdpcmCreate(TwinStudio_Arena* arena, AdpcmGetPCMfunc get, void *getpriv, AdpcmPutADPCMfunc put, void *putpriv, int32_t loopstart)
{
	TwinStudio_AdpcmSetup *set;

	set = TwinStudio_ArenaAlloc(arena, sizeof(TwinStudio_AdpcmSetup));
	if (set==NULL)
		return(NULL);

	set->s_1 = set->s_2 = 0.0;
	set->ps_1 = set->ps_2 = 0.0;

	set->curblock = 0;

	if (loopstart<0) /* disable looping (single shot) */
		set->loopstart = -1;
	else
		set->loopstart = loopstart;

	set->GetPCM = get;
	set->getpriv = getpriv;

	set->PutADPCM = put;
	set->putpriv = putpriv;
	set->pad = 0;
	return(set);
}

int TwinStudio_AdpcmEncode(TwinStudio_AdpcmSetup *set, int32_t blocks)
{
	AdpcmBlock adpcm;
	double samples[28];
	int32_t procblocks;

	for (procblocks=0;procblocks<blocks;procblocks++)
	{
		int32_t ret;
		adpcm.flags = 0;

		for (int32_t j=0;j<28;j++)
			samples[j] = 0.0;

		ret = set->GetPCM(set->getpriv, samples, 28);
		if (ret<0)
			return(-1);
		if (ret<28)
		{
			printf("loop end!\n");
			adpcm.flags = ADPCM_LOOP_END;
		}

		if (set->loopstart>=0)
		{
			adpcm.flags |= ADPCM_LOOP;
			if (set->curblock == set->loopstart)
			{
				printf("loop start!\n");
				adpcm.flags |= ADPCM_LOOP_START;
			}
		}


		find_predict(set, &adpcm, samples);
		pack(set, &adpcm, samples);

		if (set->PutADPCM(set->putpriv, &adpcm, 16)<0)
			return(-1);

		set->curblock++;
		if (ret<28)
			break;
	}

	if (set->loopstart<0 && procblocks<blocks)
	{
		/* this block essentialy loops to itself and contains no data */
		adpcm.predict = 0;
		adpcm.shift = 0;
		adpcm.flags = ADPCM_LOOP_START | ADPCM_LOOP | ADPCM_LOOP_END;

		for (int i=0;i<14;i++)
			adpcm.sample[i] = 0;
		if (set->PutADPCM(set->putpriv, &adpcm, 16)<0)
			return(-1);
		set->curblock++;
	}

	if (procblocks<blocks && set->pad)
	{
		int padblocks = blocks-(set->curblock%blocks);

		adpcm.predict = 0;
		adpcm.shift = 0;
		adpcm.flags = ADPCM_LOOP_START | ADPCM_LOOP | ADPCM_LOOP_END;
		for (int i=0;i<14;i++)
			adpcm.sample[i] = 0;


		for (int i=0;i<padblocks;i++)
		{
			if (set->PutADPCM(set->putpriv, &adpcm, 16) < 0) return(-1);
			set->curblock++;
		}
	}

	return(procblocks);
}

static double f[5][2] =
{
	{           0.0, 0.0},
   	{  -60.0 / 64.0, 0.0},
   	{ -115.0 / 64.0, 52.0 / 64.0},
   	{  -98.0 / 64.0, 55.0 / 64.0},
   	{ -122.0 / 64.0, 60.0 / 64.0}
};
static double revF[5][2] =
{
	{           0.0, 0.0},
   	{  60.0 / 64.0, 0.0},
   	{ 115.0 / 64.0, -52.0 / 64.0},
   	{  98.0 / 64.0, -55.0 / 64.0},
   	{ 122.0 / 64.0, -60.0 / 64.0}
};
#define MAX(_a,_b)   ((_a)>(_b) ? (_a):(_b))
#define MIN(_a,_b)   ((_a)<(_b) ? (_a):(_b))
#define CLAMP(_v,_min,_max) MIN(MAX(_v,_min),_max);

static void find_predict(TwinStudio_AdpcmSetup *set, AdpcmBlock *adpcm, double *samples)
{
	double buffer[28][5];
	double min = 1e10;
	double max[5];
	double ds;
	int32_t min2;
	int32_t shift_mask;
	double s_0, s_1, s_2;

	for (int32_t i=0;i<5;i++)
	{
		max[i] = 0.0;
		s_1 = set->s_1;
		s_2 = set->s_2;

		for (int32_t j=0;j<28;j++)
		{
			s_0 = CLAMP(samples[j],-30720.0,30719.0);

			ds = s_0 + s_1 * f[i][0] + s_2 * f[i][1];
			buffer[j][i] = ds;
			if (fabs(ds) > max[i])
				max[i] = fabs(ds);

			s_2 = s_1;
			s_1 = s_0;
		}

		if ( max[i] < min )
		{
			min = max[i];
			adpcm->predict = i;
		}
		if (min <= 7)
		{
			adpcm->predict = 0;
			break;
		}
	}

	set->s_1 = s_1;
	set->s_2 = s_2;

	for (int32_t i=0;i<28;i++ )
		samples[i] = buffer[i][adpcm->predict];

	min2 = (int32_t)min;
	shift_mask = 0x4000;
	adpcm->shift = 0;

	while(adpcm->shift < 12)
	{
		if (shift_mask & ( min2 + (shift_mask>>3) ))
			break;
		adpcm->shift++;
		shift_mask = shift_mask >> 1;
	}
}

static void pack(TwinStudio_AdpcmSetup *set, AdpcmBlock *adpcm, const double *samples)
{
	double s_1, s_2;
	int16_t four_bit[28];

	s_1 = set->ps_1;
	s_2 = set->ps_2;

	for (int32_t i=0;i<28;i++)
	{
		double ds;
		int32_t di;
		double s_0;

		s_0 = samples[i] + s_1 * f[adpcm->predict][0] + s_2 * f[adpcm->predict][1];
		ds = s_0 * (double) (1<<adpcm->shift);

		di = ((int32_t)ds+0x800) & 0xfffff000;
		di = CLAMP(di,-32768,32767);

		four_bit[i] = (int16_t)di;

		di = di >> adpcm->shift;
		s_2 = s_1;
		s_1 = (double) di - s_0;
	}

	for (int32_t i=0;i<14;i++)
		adpcm->sample[i] = ( ( four_bit[(i*2)+1] >> 8 ) & 0xf0 ) | ( ( four_bit[i*2] >> 12 ) & 0xf );

	set->ps_1 = s_1;
	set->ps_2 = s_2;
}


static int16_t SampleToPCM(int32_t sample, int32_t factor, int32_t predict, float* s0, float* s1)
{
    sample <<= 12;
    sample = (int16_t)sample;
    sample >>= factor;
    float value = sample;
    value += *s0 * revF[predict][0];
    value += *s1 * revF[predict][1];
    *s1 = *s0;
    *s0 = value;
    return (int16_t)round(value);
}


static uint8_t LineToPCM(TwinStudio_BinarySerializer* reader, TwinStudio_BinarySerializer* writer, float* s0, float* s1)
{
    uint8_t startByte = TwinStudio_BinReadUInt8(reader);
    uint8_t flags = TwinStudio_BinReadUInt8(reader);
    int32_t factor = startByte & 0xF;
    int32_t predict = (startByte >> 4) & 0xF;
    if ((flags & ADPCM_LOOP_END) == 0)
    {
        for (int32_t i = 0; i < 14; ++i)
        {
            uint8_t src = TwinStudio_BinReadUInt8(reader);
            int32_t low = src & 0xF;
            int32_t high = (src & 0xF0) >> 4;
            int16_t l = SampleToPCM(low, factor, predict, s0, s1);
            int16_t h = SampleToPCM(high, factor, predict, s0, s1);
            TwinStudio_BinWriteInt16(writer, l);
            TwinStudio_BinWriteInt16(writer, h);
        }
    }

    return flags;
}


static TwinStudio_AdpcmDecodeResult AdpcmDecodeMono(TwinStudio_Arena* arena, void* adpcm, size_t size)
{
	int32_t loopPosition = -1;
    void* resultData = TwinStudio_ArenaAlloc(arena, size * 4);
    TwinStudio_BinarySerializer* writer = TwinStudio_BinSerializerAllocate(resultData, TwinStudio_BinarySerializerModeWrite, size * 4, false);
    TwinStudio_BinarySerializer* reader = TwinStudio_BinSerializerAllocate(adpcm, TwinStudio_BinarySerializerModeRead, size, false);
    float s0 = 0.0f;
    float s1 = 0.0f;
    uint8_t flag = 0;
	uint32_t sampleIndex = 0;
	const uint32_t maxSamples = size / 16;
    while ((flag & ADPCM_LOOP_END) == 0 && sampleIndex < maxSamples)
    {
        flag = LineToPCM(reader, writer, &s0, &s1);
		if ((flag & ADPCM_LOOP_START) != 0 && loopPosition == -1)
		{
			loopPosition = sampleIndex;
			printf("loop marker found!\n");
		}
		sampleIndex++;
    }

    TwinStudio_AdpcmDecodeResult result = { .pcmData = resultData, .pcmDataSize = TwinStudio_BinGetStreamPosition(writer), .loopPosition = loopPosition };

    TwinStudio_BinSerializerFree(reader);
    TwinStudio_BinSerializerFree(writer);
    return result;
}


static int16_t SampleToPCM2(int32_t sample, int32_t factor, int32_t predict, double* s0, double* s1)
{
    sample <<= 12;
    sample = (int16_t)sample;
    sample >>= factor;
    double value = sample;
    value += *s0 * revF[predict][0];
    value += *s1 * revF[predict][1];
    *s1 = *s0;
    *s0 = value;
    return (int16_t)round(value);
}


static void LineToPCM2(uint8_t* output, uint8_t* input, double* s0, double* s1)
{
    int32_t factor = input[0] & 0xF;
    int32_t predict = (input[0] >> 4) & 0xF;
    for (int32_t i = 0; i < 14; ++i)
    {
        int32_t adl = input[i + 2] & 0xF;
        int32_t adh = (input[i + 2] & 0xF0) >> 4;
        int16_t l = SampleToPCM2(adl, factor, predict, s0, s1);
        int16_t h = SampleToPCM2(adh, factor, predict, s0, s1);
        memcpy(output + i * 4 + 0, &l, sizeof(int16_t));
        memcpy(output + i * 4 + 2, &h, sizeof(int16_t));
    }
}


static TwinStudio_AdpcmDecodeResult AdpcmDecodeStereo(TwinStudio_Arena* arena, void* adpcm, size_t size, uint32_t interleave)
{
    assert(size % 32 == 0);
    assert(interleave % 16 == 0);

	int32_t loopPosition = -1;
    void* resultData = TwinStudio_ArenaAlloc(arena, size * 4);
    TwinStudio_BinarySerializer* writer = TwinStudio_BinSerializerAllocate(resultData, TwinStudio_BinarySerializerModeWrite, size * 4, false);

    double s0_l = 0, s1_l = 0;
    double s0_r = 0, s1_r = 0;
    int32_t interleave_adv = 0;
    int32_t stereo_size = size / 32;
    for (int32_t i = 0; i < stereo_size; ++i)
    {
        if ((i % interleave) == 0)
        {
            interleave_adv++;
        }

        uint8_t line_l[16];
        uint8_t line_r[16];
        memcpy(line_l, ((char*)adpcm) + (i + interleave * (interleave_adv - 1)) * 16, 16);
        memcpy(line_r, ((char*)adpcm) + (i + interleave * (interleave_adv)) * 16, 16);
		if (((line_l[1] & ADPCM_LOOP_START) != 0 || (line_r[1] & ADPCM_LOOP_START) != 0) && loopPosition == -1)
		{
			loopPosition = i;
			printf("loop marker found!\n");
		}

        if (line_l[1] == ADPCM_FILE_END || line_r[1] == ADPCM_FILE_END)
        {
            break;
        }

        uint8_t l[56];
        uint8_t r[56];
        LineToPCM2(l, line_l, &s0_l, &s1_l);
        LineToPCM2(r, line_r, &s0_r, &s1_r);

        for (int32_t j = 0; j < 28; ++j)
        {
            TwinStudio_BinWriteUInt8(writer, l[0 + j * 2]);
            TwinStudio_BinWriteUInt8(writer, l[1 + j * 2]);
            TwinStudio_BinWriteUInt8(writer, r[0 + j * 2]);
            TwinStudio_BinWriteUInt8(writer, r[1 + j * 2]);
        }

        if (line_l[1] == ADPCM_LOOP_END || line_r[1] == ADPCM_LOOP_END)
        {
            break;
        }
    }

    TwinStudio_AdpcmDecodeResult result = { .pcmData = resultData, .pcmDataSize = TwinStudio_BinGetStreamPosition(writer), .loopPosition = loopPosition };

    TwinStudio_BinSerializerFree(writer);
    return result;
}


TwinStudio_AdpcmDecodeResult TwinStudio_AdpcmDecode(TwinStudio_Arena* arena, void* adpcm, size_t size, uint32_t interleave)
{
    if (interleave == 0)
    {
        return AdpcmDecodeMono(arena, adpcm, size);
    }

    return AdpcmDecodeStereo(arena, adpcm, size, interleave / 16);
}