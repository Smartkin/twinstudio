#ifndef TS_DMA_TAG_H
#define TS_DMA_TAG_H

#include <stdint.h>

typedef enum {
    TS_DIT_REFE = 0b000,
    TS_DIT_CNT  = 0b001,
    TS_DIT_NEXT = 0b010,
    TS_DIT_REF  = 0b011,
    TS_DIT_REFS = 0b100,
    TS_DIT_CALL = 0b101,
    TS_DIT_RET  = 0b110,
    TS_DIT_END  = 0b111
} TwinStudio_DmaIdType;


typedef struct TwinStudio_DmaTag {
    uint16_t qwc   : 16;
    uint16_t __pad : 10;
    uint8_t pce    : 2;
    uint8_t id     : 3;
    uint8_t irq    : 1;
    uint32_t addr  : 31;
    uint8_t spr    : 1;
    uint64_t extra;
} TwinStudio_DmaTag;

#endif // TS_DMA_TAG_H