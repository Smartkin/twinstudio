/*
# _____     ___ ____     ___ ____
#  ____|   |    ____|   |        | |____|
# |     ___|   |____ ___|    ____| |    \    PS2DEV Open Source Project.
#-----------------------------------------------------------------------
# Copyright 2005, James Lee (jbit<at>jbit<dot>net)
# Licenced under Academic Free License version 2.0
# Review ps2sdk README & LICENSE files for further details.
*/

#ifndef TS_ADPCM_H_
#define TS_ADPCM_H_

#include <stdint.h>
#include "memory/memory.h"

typedef int32_t (*AdpcmGetPCMfunc)  (void *priv, double *pcm, int32_t len); /* Length in samples */
typedef int32_t (*AdpcmPutADPCMfunc)(void *priv, void  *data, int32_t len); /* Length in bytes */

typedef struct
{
	AdpcmGetPCMfunc   GetPCM;
	AdpcmPutADPCMfunc PutADPCM;
	double s_1, s_2;
	double ps_1, ps_2;
	int32_t curblock;  /* Current block */
	int32_t loopstart; /* Loop start position, in ADPCM blocks (28 PCM samples) */
	void *getpriv;
	void *putpriv;
	int32_t pad;
} TwinStudio_AdpcmSetup;

typedef struct {
    void* pcmData;
    size_t pcmDataSize;
	uint32_t loopPosition;
} TwinStudio_AdpcmDecodeResult;

typedef struct
{
	int32_t  position;
	int32_t  channel;
	int32_t  channelCount;
	int16_t* sample;
	int32_t  sampleCount;
} TwinStudio_PcmBuffer;

extern TwinStudio_AdpcmSetup *TwinStudio_AdpcmCreate(TwinStudio_Arena* arena, AdpcmGetPCMfunc get, void *getpriv, AdpcmPutADPCMfunc put, void *putpriv, int32_t loopstart);
extern int TwinStudio_AdpcmEncode(TwinStudio_AdpcmSetup *set, int32_t blocks);
// if interleave is 0 then decoded as mono
extern TwinStudio_AdpcmDecodeResult TwinStudio_AdpcmDecode(TwinStudio_Arena* arena, void* adpcm, size_t size, uint32_t interleave);


#endif // TS_ADPCM_H_