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

typedef int (*AdpcmGetPCMfunc)  (void *priv, double *pcm, int len); /* Length in samples */
typedef int (*AdpcmPutADPCMfunc)(void *priv, void  *data, int len); /* Length in bytes */

typedef struct
{
	AdpcmGetPCMfunc   GetPCM;
	AdpcmPutADPCMfunc PutADPCM;
	double s_1, s_2;
	double ps_1, ps_2;
	int curblock;  /* Current block */
	int loopstart; /* Loop start position, in ADPCM blocks (28 PCM samples) */
	void *getpriv;
	void *putpriv;
	int pad;
} TwinStudio_AdpcmSetup;

typedef struct {
    void* pcmData;
    size_t pcmDataSize;
} TwinStudio_AdpcmDecodeResult;

extern TwinStudio_AdpcmSetup *TwinStudio_AdpcmCreate(TwinStudio_Arena* arena, AdpcmGetPCMfunc get, void *getpriv, AdpcmPutADPCMfunc put, void *putpriv, int loopstart);
extern int TwinStudio_AdpcmEncode(TwinStudio_AdpcmSetup *set, int blocks);
// if interleave is 0 then decoded as mono
extern TwinStudio_AdpcmDecodeResult TwinStudio_AdpcmDecode(TwinStudio_Arena* arena, void* adpcm, size_t size, uint32_t interleave);


#endif // TS_ADPCM_H_