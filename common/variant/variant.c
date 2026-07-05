#include "variant.h"
#include <assert.h>


inline bool __TwinStudio_VariantGet_bool(TwinStudio_Variant variant)
{
    assert(variant.type == TwinStudio_VariantBool);

    return variant.storage.isEnabled;
}


int8_t __TwinStudio_VariantGet_int8_t(TwinStudio_Variant variant)
{
    assert(variant.type == TwinStudio_VariantInt8 || variant.type == TwinStudio_VariantEnum);

    return variant.storage.integer8;
}


uint8_t __TwinStudio_VariantGet_uint8_t(TwinStudio_Variant variant)
{
    assert(variant.type == TwinStudio_VariantUInt8 || variant.type == TwinStudio_VariantEnum);

    return variant.storage.uinteger8;
}


int16_t __TwinStudio_VariantGet_int16_t(TwinStudio_Variant variant)
{
    assert(variant.type == TwinStudio_VariantInt16 || variant.type == TwinStudio_VariantEnum);

    return variant.storage.integer16;
}


uint16_t __TwinStudio_VariantGet_uint16_t(TwinStudio_Variant variant)
{
    assert(variant.type == TwinStudio_VariantInt16 || variant.type == TwinStudio_VariantEnum);

    return variant.storage.uinteger16;
}


inline int32_t __TwinStudio_VariantGet_int32_t(TwinStudio_Variant variant)
{
    assert(variant.type == TwinStudio_VariantInt32 || variant.type == TwinStudio_VariantEnum);

    return variant.storage.integer;
}


uint32_t __TwinStudio_VariantGet_uint32_t(TwinStudio_Variant variant)
{
    assert(variant.type == TwinStudio_VariantUInt32 || variant.type == TwinStudio_VariantEnum);

    return variant.storage.uinteger;
}


int64_t __TwinStudio_VariantGet_int64_t(TwinStudio_Variant variant)
{
    assert(variant.type == TwinStudio_VariantInt64 || variant.type == TwinStudio_VariantEnum);

    return variant.storage.integer64;
}



uint64_t __TwinStudio_VariantGet_uint64_t(TwinStudio_Variant variant)
{
    assert(variant.type == TwinStudio_VariantUInt64 || variant.type == TwinStudio_VariantEnum);

    return variant.storage.uinteger64;
}


inline float __TwinStudio_VariantGet_float(TwinStudio_Variant variant)
{
    assert(variant.type == TwinStudio_VariantFloat);

    return variant.storage.floating;
}


inline TwinStudio_StringView __TwinStudio_VariantGet_TwinStudio_StringView(TwinStudio_Variant variant)
{
    assert(variant.type == TwinStudio_VariantString);

    return variant.storage.string;
}


inline void* __TwinStudio_VariantGet_object(TwinStudio_Variant variant)
{
    assert(variant.type == TwinStudio_VariantObject);

    return variant.storage.object;
}


inline void TwinStudio_VariantSetBool(TwinStudio_Variant* variant, bool value)
{
    variant->type = TwinStudio_VariantBool;
    variant->storage.isEnabled = value;
}


inline void TwinStudio_VariantSetInt8(TwinStudio_Variant* variant, int8_t value)
{
    variant->type = TwinStudio_VariantInt8;
    variant->storage.integer8 = value;
}


inline void TwinStudio_VariantSetUInt8(TwinStudio_Variant* variant, uint8_t value)
{
    variant->type = TwinStudio_VariantUInt8;
    variant->storage.uinteger8 = value;
}


inline void TwinStudio_VariantSetInt16(TwinStudio_Variant* variant, int16_t value)
{
    variant->type = TwinStudio_VariantInt16;
    variant->storage.integer16 = value;
}



inline void TwinStudio_VariantSetUInt16(TwinStudio_Variant* variant, uint16_t value)
{
    variant->type = TwinStudio_VariantUInt16;
    variant->storage.uinteger16 = value;
}


inline void TwinStudio_VariantSetInt32(TwinStudio_Variant* variant, int32_t value)
{
    variant->type = TwinStudio_VariantInt32;
    variant->storage.integer = value;
}


inline void TwinStudio_VariantSetUInt32(TwinStudio_Variant* variant, uint32_t value)
{
    variant->type = TwinStudio_VariantUInt32;
    variant->storage.uinteger = value;
}


inline void TwinStudio_VariantSetInt64(TwinStudio_Variant* variant, int64_t value)
{
    variant->type = TwinStudio_VariantInt64;
    variant->storage.integer64 = value;
}



inline void TwinStudio_VariantSetUInt64(TwinStudio_Variant* variant, uint64_t value)
{
    variant->type = TwinStudio_VariantUInt64;
    variant->storage.uinteger64 = value;
}


inline void TwinStudio_VariantSetFloat(TwinStudio_Variant* variant, float value)
{
    variant->type = TwinStudio_VariantFloat;
    variant->storage.floating = value;
}


inline void TwinStudio_VariantSetString(TwinStudio_Variant* variant, TwinStudio_StringView value)
{
    variant->type = TwinStudio_VariantString;
    variant->storage.string = value;
}


inline void TwinStudio_VariantSetObject(TwinStudio_Variant* variant, void* value)
{
    variant->type = TwinStudio_VariantObject;
    variant->storage.object = value;
}