#include "math.h"
#include <stdlib.h>

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


float TwinStudio_Abs(float f)
{
    if (f > 0)
    {
        return f;
    }

    return -f;
}


bool TwinStudio_FloatEqual(float f1, float f2)
{
    return TwinStudio_FloatEqualE(f1, f2, FLOAT_EPSILON);
}


bool TwinStudio_FloatEqualE(float f1, float f2, float eps)
{
    return TwinStudio_Abs(f1 - f2) <= eps;
}