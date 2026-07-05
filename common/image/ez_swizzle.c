#include <stdint.h>
#include "ez_swizzle.h"

static const int32_t block32[32] = {
        0,  1,  4,  5, 16, 17, 20, 21,
        2,  3,  6,  7, 18, 19, 22, 23,
        8,  9, 12, 13, 24, 25, 28, 29,
    10, 11, 14, 15, 26, 27, 30, 31
};

static const int32_t columnWord32[16] = {
        0,  1,  4,  5,  8,  9, 12, 13,
        2,  3,  6,  7, 10, 11, 14, 15
};

static const int32_t block16[32] = {
        0,  2,  8, 10,
        1,  3,  9, 11,
        4,  6, 12, 14,
        5,  7, 13, 15,
    16, 18, 24, 26,
    17, 19, 25, 27,
    20, 22, 28, 30,
    21, 23, 29, 31
};

static const int32_t columnWord16[32] = {
        0,  1,  4,  5,  8,  9, 12, 13,   0,  1,  4,  5,  8,  9, 12, 13,
        2,  3,  6,  7, 10, 11, 14, 15,   2,  3,  6,  7, 10, 11, 14, 15
};

static const int32_t columnHalf16[32] = {
    0, 0, 0, 0, 0, 0, 0, 0,  1, 1, 1, 1, 1, 1, 1, 1,
    0, 0, 0, 0, 0, 0, 0, 0,  1, 1, 1, 1, 1, 1, 1, 1
};


static const int32_t block8[32] = {
        0,  1,  4,  5, 16, 17, 20, 21,
        2,  3,  6,  7, 18, 19, 22, 23,
        8,  9, 12, 13, 24, 25, 28, 29,
    10, 11, 14, 15, 26, 27, 30, 31
};

static const int32_t columnWord8[2][64] = {
    {
            0,  1,  4,  5,  8,  9, 12, 13,   0,  1,  4,  5,  8,  9, 12, 13,
            2,  3,  6,  7, 10, 11, 14, 15,   2,  3,  6,  7, 10, 11, 14, 15,

            8,  9, 12, 13,  0,  1,  4,  5,   8,  9, 12, 13,  0,  1,  4,  5,
        10, 11, 14, 15,  2,  3,  6,  7,  10, 11, 14, 15,  2,  3,  6,  7
    },
    {
            8,  9, 12, 13,  0,  1,  4,  5,   8,  9, 12, 13,  0,  1,  4,  5,
        10, 11, 14, 15,  2,  3,  6,  7,  10, 11, 14, 15,  2,  3,  6,  7,

            0,  1,  4,  5,  8,  9, 12, 13,   0,  1,  4,  5,  8,  9, 12, 13,
            2,  3,  6,  7, 10, 11, 14, 15,   2,  3,  6,  7, 10, 11, 14, 15
    }
};

static const int32_t columnByte8[64] = {
    0, 0, 0, 0, 0, 0, 0, 0,  2, 2, 2, 2, 2, 2, 2, 2,
    0, 0, 0, 0, 0, 0, 0, 0,  2, 2, 2, 2, 2, 2, 2, 2,

    1, 1, 1, 1, 1, 1, 1, 1,  3, 3, 3, 3, 3, 3, 3, 3,
    1, 1, 1, 1, 1, 1, 1, 1,  3, 3, 3, 3, 3, 3, 3, 3
};

static const int32_t block4[32] = {
    0,  2,  8, 10,
    1,  3,  9, 11,
    4,  6, 12, 14,
    5,  7, 13, 15,
    16, 18, 24, 26,
    17, 19, 25, 27,
    20, 22, 28, 30,
    21, 23, 29, 31
};

static const int32_t columnWord4[2][128] = {
    {
            0,  1,  4,  5,  8,  9, 12, 13,   0,  1,  4,  5,  8,  9, 12, 13,   0,  1,  4,  5,  8,  9, 12, 13,   0,  1,  4,  5,  8,  9, 12, 13,
            2,  3,  6,  7, 10, 11, 14, 15,   2,  3,  6,  7, 10, 11, 14, 15,   2,  3,  6,  7, 10, 11, 14, 15,   2,  3,  6,  7, 10, 11, 14, 15,

            8,  9, 12, 13,  0,  1,  4,  5,   8,  9, 12, 13,  0,  1,  4,  5,   8,  9, 12, 13,  0,  1,  4,  5,   8,  9, 12, 13,  0,  1,  4,  5,
        10, 11, 14, 15,  2,  3,  6,  7,  10, 11, 14, 15,  2,  3,  6,  7,  10, 11, 14, 15,  2,  3,  6,  7,  10, 11, 14, 15,  2,  3,  6,  7
    },
    {
            8,  9, 12, 13,  0,  1,  4,  5,   8,  9, 12, 13,  0,  1,  4,  5,   8,  9, 12, 13,  0,  1,  4,  5,   8,  9, 12, 13,  0,  1,  4,  5,
        10, 11, 14, 15,  2,  3,  6,  7,  10, 11, 14, 15,  2,  3,  6,  7,  10, 11, 14, 15,  2,  3,  6,  7,  10, 11, 14, 15,  2,  3,  6,  7,

            0,  1,  4,  5,  8,  9, 12, 13,   0,  1,  4,  5,  8,  9, 12, 13,   0,  1,  4,  5,  8,  9, 12, 13,   0,  1,  4,  5,  8,  9, 12, 13,
            2,  3,  6,  7, 10, 11, 14, 15,   2,  3,  6,  7, 10, 11, 14, 15,   2,  3,  6,  7, 10, 11, 14, 15,   2,  3,  6,  7, 10, 11, 14, 15
    }
};

static const int32_t columnByte4[128] = {
    0, 0, 0, 0, 0, 0, 0, 0,  2, 2, 2, 2, 2, 2, 2, 2,  4, 4, 4, 4, 4, 4, 4, 4,  6, 6, 6, 6, 6, 6, 6, 6,
    0, 0, 0, 0, 0, 0, 0, 0,  2, 2, 2, 2, 2, 2, 2, 2,  4, 4, 4, 4, 4, 4, 4, 4,  6, 6, 6, 6, 6, 6, 6, 6,

    1, 1, 1, 1, 1, 1, 1, 1,  3, 3, 3, 3, 3, 3, 3, 3,  5, 5, 5, 5, 5, 5, 5, 5,  7, 7, 7, 7, 7, 7, 7, 7,
    1, 1, 1, 1, 1, 1, 1, 1,  3, 3, 3, 3, 3, 3, 3, 3,  5, 5, 5, 5, 5, 5, 5, 5,  7, 7, 7, 7, 7, 7, 7, 7
};


static int32_t MapCoords8(int32_t x, int32_t y, int32_t width, int32_t* cb)
{
    int32_t pageX = x / 128;
    int32_t pageY = y / 64;
    int32_t page = pageX + pageY * width;

    int32_t px = x - (pageX * 128);
    int32_t py = y - (pageY * 64);

    int32_t blockX = px / 16;
    int32_t blockY = py / 16;
    int32_t block = block8[blockX + blockY * 8];

    int32_t bx = px - (blockX * 16);
    int32_t by = py - (blockY * 16);

    int32_t column = by / 4;

    int32_t cx = bx;
    int32_t cy = by - column * 4;
    int32_t cw = columnWord8[column & 1][cx + cy * 16];
    *cb = columnByte8[cx + cy * 16];

    return page * 2048 + block * 64 + column * 16 + cw;
}


static int32_t MapCoords16(int32_t x, int32_t y, int32_t width, int32_t* ch)
{
    int32_t pageX = x / 64;
    int32_t pageY = y / 64;
    int32_t page = pageX + pageY * width;

    int32_t px = x - (pageX * 64);
    int32_t py = y - (pageY * 64);

    int32_t blockX = px / 16;
    int32_t blockY = py / 8;
    int32_t block = block16[blockX + blockY * 4];

    int32_t bx = px - blockX * 16;
    int32_t by = py - blockY * 8;

    int32_t column = by / 2;

    int32_t cx = bx;
    int32_t cy = by - column * 2;
    int32_t cw = columnWord16[cx + cy * 16];
    *ch = columnHalf16[cx + cy * 16];

    return page * 2048 + block * 64 + column * 16 + cw;
}

static int32_t MapCoords32(int32_t x, int32_t y, int32_t width)
{
    int32_t pageX = x / 64;
    int32_t pageY = y / 32;
    int32_t page = pageX + pageY * width;

    int32_t px = x - (pageX * 64);
    int32_t py = y - (pageY * 32);

    int32_t blockX = px / 8;
    int32_t blockY = py / 8;
    int32_t block = block32[blockX + blockY * 8];

    int32_t bx = px - blockX * 8;
    int32_t by = py - blockY * 8;

    int32_t column = by / 2;

    int32_t cx = bx;
    int32_t cy = by - column * 2;
    int32_t cw = columnWord32[cx + cy * 8];

    return page * 2048 + block * 64 + column * 16 + cw;
}


void TwinStudio_ReadTexPSMT8(uint8_t* destination, int32_t dbp, int32_t dbw, int32_t dsax, int32_t dsay, int32_t rrw, int32_t rrh, uint8_t* source)
{
    dbw >>= 1;
    int32_t src = 0;
    int32_t startBlockPos = dbp * 64;

    for (int32_t y = dsay; y < dsay + rrh; y++)
    {
        for (int32_t x = dsax; x < dsax + rrw; x++)
        {
            int32_t cb = 0;
            int32_t dst = startBlockPos + MapCoords8(x, y, dbw, &cb);
            destination[src] = source[4 * dst + cb];
            src++;
        }
    }
}


void TwinStudio_WriteTexPSMT8(uint8_t* destination, int32_t dbp, int32_t dbw, int32_t dsax, int32_t dsay, int32_t rrw, int32_t rrh, uint8_t* source)
{
    dbw >>= 1;
    int32_t src = 0;
    int32_t startBlockPos = dbp * 64;

    for (int32_t y = dsay; y < dsay + rrh; y++)
    {
        for (int32_t x = dsax; x < dsax + rrw; x++)
        {
            int32_t cb = 0;
            int32_t dst = startBlockPos + MapCoords8(x, y, dbw, &cb);
            destination[4 * dst + cb] = source[src];
            src++;
        }
    }
}


void TwinStudio_ReadTexPSMCT16(uint8_t* destination, int32_t dbp, int32_t dbw, int32_t dsax, int32_t dsay, int32_t rrw, int32_t rrh, uint8_t* source)
{
    int32_t src = 0;
    int32_t startBlockPos = dbp * 64;

    for (int32_t y = dsay; y < dsay + rrh; y++)
    {
        for (int32_t x = dsax; x < dsax + rrw; x++)
        {
            int32_t ch = 0;
            int32_t dst = startBlockPos + MapCoords16(x, y, dbw, &ch);
            for (int32_t i = 0; i < 2; i++)
            {
                destination[src + i] = source[4 * dst + 2 * ch + i];
            }
            src += 2;
        }
    }
}


void TwinStudio_WriteTexPSMCT16(uint8_t* destination, int32_t dbp, int32_t dbw, int32_t dsax, int32_t dsay, int32_t rrw, int32_t rrh, uint8_t* source)
{
    int32_t src = 0;
    int32_t startBlockPos = dbp * 64;

    for (int32_t y = dsay; y < dsay + rrh; y++)
    {
        for (int32_t x = dsax; x < dsax + rrw; x++)
        {
            int32_t ch = 0;
            int32_t dst = startBlockPos + MapCoords16(x, y, dbw, &ch);
            for (int32_t i = 0; i < 2; i++)
            {
                destination[4 * dst + 2 * ch + i] = source[src + i];
            }
            src += 2;
        }
    }
}


void TwinStudio_ReadTexPSMCT32(uint8_t* destination, int32_t dbp, int32_t dbw, int32_t dsax, int32_t dsay, int32_t rrw, int32_t rrh, uint8_t* source)
{
    int32_t src = 0;
    int32_t startBlockPos = dbp * 64;

    for (int32_t y = dsay; y < dsay + rrh; y++)
    {
        for (int32_t x = dsax; x < dsax + rrw; x++)
        {
            int32_t dst = startBlockPos + MapCoords32(x, y, dbw);
            for (int32_t i = 0; i < 4; i++)
            {
                destination[src + i] = source[4 * dst + i];
            }
            src += 4;
        }
    }
}


void TwinStudio_WriteTexPSMCT32(uint8_t* destination, int32_t dbp, int32_t dbw, int32_t dsax, int32_t dsay, int32_t rrw, int32_t rrh, uint8_t* source)
{
    int32_t src = 0;
    int32_t startBlockPos = dbp * 64;

    for (int32_t y = dsay; y < dsay + rrh; y++)
    {
        for (int32_t x = dsax; x < dsax + rrw; x++)
        {
            int32_t dst = startBlockPos + MapCoords32(x, y, dbw);
            for (int32_t i = 0; i < 4; i++)
            {
                destination[4 * dst + i] = source[src + i];
            }
            src += 4;
        }
    }
}