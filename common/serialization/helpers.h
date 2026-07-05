#ifndef TS_DESCS_RAYLIB_HELPERS_H
#define TS_DESCS_RAYLIB_HELPERS_H


#include <raylib.h>
#include <cJSON.h>
#include "binary_serializer.h"
#include "memory/memory.h"

void Vector2BinSerialize(Vector2* v, TwinStudio_BinarySerializer* serializer, TwinStudio_Arena* arena, size_t size, void* userData);
void Vector3BinSerialize(Vector3* v, TwinStudio_BinarySerializer* serializer, TwinStudio_Arena* arena, size_t size, void* userData);
void Vector4BinSerialize(Vector4* v, TwinStudio_BinarySerializer* serializer, TwinStudio_Arena* arena, size_t size, void* userData);
void MatrixBinSerialize(Matrix* mat, TwinStudio_BinarySerializer* serializer, TwinStudio_Arena* arena, size_t size, void* userData);
void QuaternionBinSerialize(Quaternion* q, TwinStudio_BinarySerializer* serializer, TwinStudio_Arena* arena, size_t size, void* userData);

void Vector2BinDeserialize(Vector2* v, TwinStudio_BinarySerializer* serializer, TwinStudio_Arena* arena, size_t size, void* userData);
void Vector3BinDeserialize(Vector3* v, TwinStudio_BinarySerializer* serializer, TwinStudio_Arena* arena, size_t size, void* userData);
void Vector4BinDeserialize(Vector4* v, TwinStudio_BinarySerializer* serializer, TwinStudio_Arena* arena, size_t size, void* userData);
void MatrixBinDeserialize(Matrix* mat, TwinStudio_BinarySerializer* serializer, TwinStudio_Arena* arena, size_t size, void* userData);
void QuaternionBinDeserialize(Quaternion* q, TwinStudio_BinarySerializer* serializer, TwinStudio_Arena* arena, size_t size, void* userData);


cJSON* Vector2JsonSerialize(Vector2* v);
cJSON* Vector3JsonSerialize(Vector3* v);
cJSON* Vector4JsonSerialize(Vector4* v);
cJSON* MatrixJsonSerialize(Matrix* mat);
cJSON* QuaternionJsonSerialize(Quaternion* q);

void Vector2JsonDeserialize(Vector2* v, cJSON* source);
void Vector3JsonDeserialize(Vector3* v, cJSON* source);
void Vector4JsonDeserialize(Vector4* v, cJSON* source);
void MatrixJsonDeserialize(Matrix* mat, cJSON* source);
void QuaternionJsonDeserialize(Quaternion* q, cJSON* source);

uint32_t ToBigEndian(uint32_t value);

#endif // TS_DESCS_RAYLIB_HELPERS_H