#include "math.h"


int32_t TwinStudio_MinInt(int32_t i1, int32_t i2)
{
    return i1 < i2 ? i1 : i2;
}


int32_t TwinStudio_MaxInt(int32_t i1, int32_t i2)
{
    return i1 < i2 ? i2 : i1;
}


uint32_t TwinStudio_MinUInt(uint32_t i1, uint32_t i2)
{
    return i1 < i2 ? i1 : i2;
}


uint32_t TwinStudio_MaxUInt(uint32_t i1, uint32_t i2)
{
    return i1 < i2 ? i2 : i1;
}