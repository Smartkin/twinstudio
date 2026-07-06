#include "helpers.h"
#include "cJSON.h"
#include "serialization/binary_serializer.h"
#include <stdio.h>


void Vector2BinSerialize(Vector2* v, TwinStudio_BinarySerializer* serializer, TwinStudio_Arena* arena, size_t size, void* userData)
{
    TwinStudio_BinWriteAny(serializer, v, size);
}


void Vector3BinSerialize(Vector3* v, TwinStudio_BinarySerializer* serializer, TwinStudio_Arena* arena, size_t size, void* userData)
{
    TwinStudio_BinWriteAny(serializer, v, size);
}


void Vector4BinSerialize(Vector4* v, TwinStudio_BinarySerializer* serializer, TwinStudio_Arena* arena, size_t size, void* userData)
{
    TwinStudio_BinWriteAny(serializer, v, size);
}


void MatrixBinSerialize(Matrix* mat, TwinStudio_BinarySerializer* serializer, TwinStudio_Arena* arena, size_t size, void* userData)
{
    TwinStudio_BinWriteAny(serializer, mat, size);
}


void QuaternionBinSerialize(Quaternion* q, TwinStudio_BinarySerializer* serializer, TwinStudio_Arena* arena, size_t size, void* userData)
{
    TwinStudio_BinWriteAny(serializer, q, size);
}

void Vector2BinDeserialize(Vector2* v, TwinStudio_BinarySerializer* serializer, TwinStudio_Arena* arena, size_t size, void* userData)
{
    TwinStudio_BinReadStructDirect(serializer, v, size);
}


void Vector3BinDeserialize(Vector3* v, TwinStudio_BinarySerializer* serializer, TwinStudio_Arena* arena, size_t size, void* userData)
{
    TwinStudio_BinReadStructDirect(serializer, v, size);
}


void Vector4BinDeserialize(Vector4* v, TwinStudio_BinarySerializer* serializer, TwinStudio_Arena* arena, size_t size, void* userData)
{
    TwinStudio_BinReadStructDirect(serializer, v, size);
}


void MatrixBinDeserialize(Matrix* mat, TwinStudio_BinarySerializer* serializer, TwinStudio_Arena* arena, size_t size, void* userData)
{
    TwinStudio_BinReadStructDirect(serializer, mat, size);
}


void QuaternionBinDeserialize(Quaternion* q, TwinStudio_BinarySerializer* serializer, TwinStudio_Arena* arena, size_t size, void* userData)
{
    TwinStudio_BinReadStructDirect(serializer, q, size);
}


cJSON* Vector2JsonSerialize(Vector2* v)
{
    cJSON* vJson = cJSON_CreateObject();
    cJSON_AddNumberToObject(vJson, "x", v->x);
    cJSON_AddNumberToObject(vJson, "y", v->y);
    return vJson;
}


cJSON* Vector3JsonSerialize(Vector3* v)
{
    cJSON* vJson = cJSON_CreateObject();
    cJSON_AddNumberToObject(vJson, "x", v->x);
    cJSON_AddNumberToObject(vJson, "y", v->y);
    cJSON_AddNumberToObject(vJson, "z", v->z);
    return vJson;
}


cJSON* Vector4JsonSerialize(Vector4* v)
{
    cJSON* vJson = cJSON_CreateObject();
    cJSON_AddNumberToObject(vJson, "x", v->x);
    cJSON_AddNumberToObject(vJson, "y", v->y);
    cJSON_AddNumberToObject(vJson, "z", v->z);
    cJSON_AddNumberToObject(vJson, "w", v->w);
    return vJson;
}


cJSON* MatrixJsonSerialize(Matrix* mat)
{
    cJSON* matJson = cJSON_CreateObject();
    float* matStart = (float*)mat;
    for (uint32_t i = 0; i < 16; ++i)
    {
        char matElemName[16];
        snprintf(matElemName, 16, "m%d", i);
        cJSON_AddNumberToObject(matJson, matElemName, matStart[i]);
    }

    return matJson;
}


cJSON* QuaternionJsonSerialize(Quaternion* q)
{
    return Vector4JsonSerialize(q);
}


void Vector2JsonDeserialize(Vector2* v, cJSON* source)
{
    v->x = cJSON_GetObjectItemCaseSensitive(source, "x")->valuedouble;
    v->y = cJSON_GetObjectItemCaseSensitive(source, "y")->valuedouble;
}


void Vector3JsonDeserialize(Vector3* v, cJSON* source)
{
    v->x = cJSON_GetObjectItemCaseSensitive(source, "x")->valuedouble;
    v->y = cJSON_GetObjectItemCaseSensitive(source, "y")->valuedouble;
    v->z = cJSON_GetObjectItemCaseSensitive(source, "z")->valuedouble;
}


void Vector4JsonDeserialize(Vector4* v, cJSON* source)
{
    v->x = cJSON_GetObjectItemCaseSensitive(source, "x")->valuedouble;
    v->y = cJSON_GetObjectItemCaseSensitive(source, "y")->valuedouble;
    v->z = cJSON_GetObjectItemCaseSensitive(source, "z")->valuedouble;
    v->w = cJSON_GetObjectItemCaseSensitive(source, "w")->valuedouble;
}


void MatrixJsonDeserialize(Matrix* mat, cJSON* source)
{
    float* matElems = (float*)mat;
    for (uint32_t i = 0; i < 16; ++i)
    {
        char matElemName[16];
        snprintf(matElemName, 16, "m%d", i);
        matElems[i] = cJSON_GetObjectItemCaseSensitive(source, matElemName)->valuedouble;
    }
}


void QuaternionJsonDeserialize(Quaternion* q, cJSON* source)
{
    Vector4JsonDeserialize(q, source);
}


uint32_t ToBigEndian(uint32_t value)
{
    return ((value & 0x000000FFU) << 24) | ((value & 0x0000FF00U) << 8) | ((value & 0x00FF0000U) >> 8) | ((value & 0xFF000000U) >> 24);
}