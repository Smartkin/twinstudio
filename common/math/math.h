#ifndef TS_MATH_H
#define TS_MATH_H

#include <stdint.h>
#include <stdbool.h>

#define FLOAT_EPSILON 0.00001f

int32_t TwinStudio_MinInt(int32_t i1, int32_t i2);
int32_t TwinStudio_MaxInt(int32_t i1, int32_t i2);

uint32_t TwinStudio_MinUInt(uint32_t i1, uint32_t i2);
uint32_t TwinStudio_MaxUInt(uint32_t i1, uint32_t i2);

float TwinStudio_Abs(float f);
bool TwinStudio_FloatEqual(float f1, float f2);
bool TwinStudio_FloatEqualE(float f1, float f2, float eps);

#endif // TS_MATH_H