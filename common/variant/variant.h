#ifndef TS_VARIANT_H
#define TS_VARIANT_H

#include <stdbool.h>
#include <stdint.h>

#include "string_view/string_view.h"

typedef enum {
    TwinStudio_VariantNull   = 0b000000000000000,
    TwinStudio_VariantBool   = 0b000000000000001,
    TwinStudio_VariantInt16  = 0b000000000000010,
    TwinStudio_VariantUInt16 = 0b000000000000100,
    TwinStudio_VariantInt32  = 0b000000000001000,
    TwinStudio_VariantUInt32 = 0b000000000010000,
    TwinStudio_VariantInt64  = 0b000000000100000,
    TwinStudio_VariantUInt64 = 0b000000001000000,
    TwinStudio_VariantFloat  = 0b000000010000000,
    TwinStudio_VariantString = 0b000000100000000,
    TwinStudio_VariantEnum   = 0b000001000000000,
    TwinStudio_VariantObject = 0b000010000000000,
    TwinStudio_VariantArray  = 0b000100000000000,
    TwinStudio_VariantChar   = 0b001000000000000,
    TwinStudio_VariantUInt8  = 0b010000000000000,
    TwinStudio_VariantInt8   = 0b100000000000000,
} TwinStudio_VariantType;


typedef union TwinStudio_VariantStorage {
    bool isEnabled;
    union {
        uint64_t uinteger64;
        int64_t  integer64;
        uint32_t uinteger;
        int32_t  integer;
        uint16_t uinteger16;
        int16_t  integer16;
        uint8_t  uinteger8;
        int8_t   integer8;
    };
    float floating;
    TwinStudio_StringView string;
    void* object;
} TwinStudio_VariantStorage;


typedef struct TwinStudio_Variant {
    TwinStudio_VariantStorage storage;
    TwinStudio_VariantType type;
} TwinStudio_Variant;


#define __TS_VARIANT_CREATE_bool(expr) (TwinStudio_Variant) { .storage = { .isEnabled = (expr) }, .type = TwinStudio_VariantBool }
#define __TS_VARIANT_CREATE_int8_t(expr) (TwinStudio_Variant) { .storage = { .integer = (expr) }, .type = TwinStudio_VariantInt8 }
#define __TS_VARIANT_CREATE_uint8_t(expr) (TwinStudio_Variant) { .storage = { .integer = (expr) }, .type = TwinStudio_VariantUInt8 }
#define __TS_VARIANT_CREATE_int16_t(expr) (TwinStudio_Variant) { .storage = { .integer = (expr) }, .type = TwinStudio_VariantInt16 }
#define __TS_VARIANT_CREATE_uint16_t(expr) (TwinStudio_Variant) { .storage = { .integer = (expr) }, .type = TwinStudio_VariantUInt16 }
#define __TS_VARIANT_CREATE_int32_t(expr) (TwinStudio_Variant) { .storage = { .integer = (expr) }, .type = TwinStudio_VariantInt32 }
#define __TS_VARIANT_CREATE_uint32_t(expr) (TwinStudio_Variant) { .storage = { .integer = (expr) }, .type = TwinStudio_VariantUInt32 }
#define __TS_VARIANT_CREATE_int64_t(expr) (TwinStudio_Variant) { .storage = { .integer = (expr) }, .type = TwinStudio_VariantInt64 }
#define __TS_VARIANT_CREATE_uint64_t(expr) (TwinStudio_Variant) { .storage = { .integer = (expr) }, .type = TwinStudio_VariantUInt64 }
#define __TS_VARIANT_CREATE_float(expr) (TwinStudio_Variant) { .storage = { .floating = (expr) }, .type = TwinStudio_VariantFloat }
#define __TS_VARIANT_CREATE_TwinStudio_StringView(expr) (TwinStudio_Variant) { .storage = { .string = (expr) }, .type = TwinStudio_VariantString }
#define __TS_VARIANT_CREATE_object(expr) (TwinStudio_Variant) { .storage = { .object = (expr) }, .type = TwinStudio_VariantObject }
#define TS_VARIANT_CREATE(type, expr) __TS_VARIANT_CREATE_##type((expr))
#define TS_VARIANT_CREATE_OBJECT(expr) TS_VARIANT_CREATE(object, (expr))

bool __TwinStudio_VariantGet_bool(TwinStudio_Variant variant);
int8_t __TwinStudio_VariantGet_int8_t(TwinStudio_Variant variant);
uint8_t __TwinStudio_VariantGet_uint8_t(TwinStudio_Variant variant);
int16_t __TwinStudio_VariantGet_int16_t(TwinStudio_Variant variant);
uint16_t __TwinStudio_VariantGet_uint16_t(TwinStudio_Variant variant);
int32_t __TwinStudio_VariantGet_int32_t(TwinStudio_Variant variant);
uint32_t __TwinStudio_VariantGet_uint32_t(TwinStudio_Variant variant);
int64_t __TwinStudio_VariantGet_int64_t(TwinStudio_Variant variant);
uint64_t __TwinStudio_VariantGet_uint64_t(TwinStudio_Variant variant);
float __TwinStudio_VariantGet_float(TwinStudio_Variant variant);
TwinStudio_StringView __TwinStudio_VariantGet_TwinStudio_StringView(TwinStudio_Variant variant);
void* __TwinStudio_VariantGet_object(TwinStudio_Variant variant);


void TwinStudio_VariantSetBool(TwinStudio_Variant* variant, bool value);
void TwinStudio_VariantSetInt8(TwinStudio_Variant* variant, int8_t value);
void TwinStudio_VariantSetUInt8(TwinStudio_Variant* variant, uint8_t value);
void TwinStudio_VariantSetInt16(TwinStudio_Variant* variant, int16_t value);
void TwinStudio_VariantSetUInt16(TwinStudio_Variant* variant, uint16_t value);
void TwinStudio_VariantSetInt32(TwinStudio_Variant* variant, int32_t value);
void TwinStudio_VariantSetUInt32(TwinStudio_Variant* variant, uint32_t value);
void TwinStudio_VariantSetInt64(TwinStudio_Variant* variant, int64_t value);
void TwinStudio_VariantSetUInt64(TwinStudio_Variant* variant, uint64_t value);
void TwinStudio_VariantSetFloat(TwinStudio_Variant* variant, float value);
void TwinStudio_VariantSetString(TwinStudio_Variant* variant, TwinStudio_StringView value);
void TwinStudio_VariantSetObject(TwinStudio_Variant* variant, void* value);


#define TS_VARIANT_GET(type, variant) (__TwinStudio_VariantGet_##type((variant)))
#define TS_VARIANT_GET_OBJECT(variant) TS_VARIANT_GET(object, variant)

#endif // TS_VARIANT_H