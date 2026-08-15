#ifndef TS_DESCS_RAYLIB_HELPERS_H
#define TS_DESCS_RAYLIB_HELPERS_H


#include <raylib.h>
#include <cJSON.h>
#include "binary_serializer.h"
#include "memory/memory.h"
#include "resources/chunk_resources.h"

typedef struct TwinStudio_DeserializationContext {
    TwinStudio_ChunkResourceManager* chunkResManager;
    uint32_t curItemTwinId;
} TwinStudio_DeserializationContext;

typedef struct BoundingBox4 {
    Vector4 min;
    Vector4 max;
} BoundingBox4;

Vector2    Vector2Create();
Vector3    Vector3Create();
Vector4    Vector4Create();
BoundingBox4 BoundingBox4Create();
Matrix     MatrixCreate();
Quaternion QuaternionCreate();

void Vector2BinSerialize(Vector2* v, TwinStudio_BinarySerializer* serializer, TwinStudio_Arena* arena, size_t size, void* userData);
void Vector3BinSerialize(Vector3* v, TwinStudio_BinarySerializer* serializer, TwinStudio_Arena* arena, size_t size, void* userData);
void Vector4BinSerialize(Vector4* v, TwinStudio_BinarySerializer* serializer, TwinStudio_Arena* arena, size_t size, void* userData);
void BoundingBox4BinSerialize(BoundingBox4* bb, TwinStudio_BinarySerializer* serializer, TwinStudio_Arena* arena, size_t size, void* userData);
void MatrixBinSerialize(Matrix* mat, TwinStudio_BinarySerializer* serializer, TwinStudio_Arena* arena, size_t size, void* userData);
void QuaternionBinSerialize(Quaternion* q, TwinStudio_BinarySerializer* serializer, TwinStudio_Arena* arena, size_t size, void* userData);

void Vector2BinDeserialize(TwinStudio_DeserializationContext* ctx, Vector2* v, TwinStudio_BinarySerializer* serializer, TwinStudio_Arena* arena, size_t size, void* userData);
void Vector3BinDeserialize(TwinStudio_DeserializationContext* ctx, Vector3* v, TwinStudio_BinarySerializer* serializer, TwinStudio_Arena* arena, size_t size, void* userData);
void Vector4BinDeserialize(TwinStudio_DeserializationContext* ctx, Vector4* v, TwinStudio_BinarySerializer* serializer, TwinStudio_Arena* arena, size_t size, void* userData);
void BoundingBox4BinDeserialize(TwinStudio_DeserializationContext* ctx, BoundingBox4* bb, TwinStudio_BinarySerializer* serializer, TwinStudio_Arena* arena, size_t size, void* userData);
void MatrixBinDeserialize(TwinStudio_DeserializationContext* ctx, Matrix* mat, TwinStudio_BinarySerializer* serializer, TwinStudio_Arena* arena, size_t size, void* userData);
void QuaternionBinDeserialize(TwinStudio_DeserializationContext* ctx, Quaternion* q, TwinStudio_BinarySerializer* serializer, TwinStudio_Arena* arena, size_t size, void* userData);


cJSON* Vector2JsonSerialize(Vector2* v);
cJSON* Vector3JsonSerialize(Vector3* v);
cJSON* Vector4JsonSerialize(Vector4* v);
cJSON* BoundingBox4JsonSerialize(BoundingBox4* bb);
cJSON* MatrixJsonSerialize(Matrix* mat);
cJSON* QuaternionJsonSerialize(Quaternion* q);

void Vector2JsonDeserialize(Vector2* v, cJSON* source);
void Vector3JsonDeserialize(Vector3* v, cJSON* source);
void Vector4JsonDeserialize(Vector4* v, cJSON* source);
void BoundingBox4JsonDeserialize(BoundingBox4* bb, cJSON* source);
void MatrixJsonDeserialize(Matrix* mat, cJSON* source);
void QuaternionJsonDeserialize(Quaternion* q, cJSON* source);

uint32_t ToBigEndian(uint32_t value);

#endif // TS_DESCS_RAYLIB_HELPERS_H