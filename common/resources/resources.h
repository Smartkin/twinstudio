#ifndef TS_RESOURCES_H
#define TS_RESOURCES_H

#include <raylib.h>
#include <stdint.h>
#include <stddef.h>

#define TS_RES_TYPE_BITS 32
#define TS_RES_KEY_MAX 1024
#define TS_RES_LANG_SEP '@'

typedef void (*TwinStudio_ResourceFree)(void* data);

typedef enum {
    TS_RT_Texture = 1 << 0,
    TS_RT_Mesh = 1 << 1,
    TS_RT_Lod = 1 << 2,
    TS_RT_RigidModel = 1 << 3,
    TS_RT_Sound = 1 << 4,
    TS_RT_Material = 1 << 5,
    TS_RT_Model = 1 << 6,
    TS_RT_Skin = 1 << 7,
    TS_RT_BlendSkin = 1 << 8,
    TS_RT_Chunk = 1 << 9,
    TS_RT_Animation = 1 << 10,
    TS_RT_Object = 1 << 11,
    TS_RT_Behavior = 1 << 12,
    TS_RT_SequenceBehavior = 1 << 13,
    TS_RT_Instance = 1 << 14,
    TS_RT_Path = 1 << 15,
    TS_RT_Camera = 1 << 16,
    TS_RT_Trigger = 1 << 17,
    TS_RT_Skydome = 1 << 18,
    TS_RT_CollisionSurface = 1 << 19,
    TS_RT_Position = 1 << 20,
    TS_RT_AI_Position = 1 << 21,
    TS_RT_AI_Path = 1 << 22,
    TS_RT_Body = 1 << 23,
    TS_RT_ParticleSystem = 1 << 24,
    TS_RT_SequenceBehaviorAction = 1 << 25,
    TS_RT_DynamicScenery = 1 << 26,
    TS_RT_Scenery = 1 << 27,
    TS_RT_Collision = 1 << 28,
    TS_RT_Localized = 1 << 29,

    TS_RT_ALL = (1u << TS_RES_TYPE_BITS) - 1u
} TwinStudio_ResourceType;

typedef enum {
    TS_SRT_None = 0,
    TS_SRT_English,
    TS_SRT_French,
    TS_SRT_German,
    TS_SRT_Spanish,
    TS_SRT_Italian,
    TS_SRT_Japanese,
    TS_SRT_LangCount
} TwinStudio_SoundResourceType;

#define TS_RES_LANG_ANY (-1)
#define TS_RES_SLOT_LANG TS_RES_TYPE_BITS

typedef uint32_t TwinStudio_ResourceMask;

typedef enum {
    TS_RR_Any = 0,

    TS_RR_Reference,


} TwinStudio_ResourceRole;

typedef struct TwinStudio_ResourceLink {
    char* key;
    TwinStudio_ResourceRole role;
} TwinStudio_ResourceLink;

typedef struct TwinStudio_Resource {
    const char* key;
    TwinStudio_ResourceMask mask; // Resource type
    TwinStudio_ResourceMask kind; // Which importer/exporter to use
    TwinStudio_SoundResourceType soundType; // Localization type (none if it's any resource or simple SFX)
    TwinStudio_ResourceFree freeFun;
    uint32_t id; // Resource ID from Twinsanity's chunk
    void* data;
    int32_t refCount;

    TwinStudio_ResourceLink* links;

    int32_t slot[TS_RES_TYPE_BITS + 1];
    uint32_t queryStamp;
} TwinStudio_Resource;

typedef struct TwinStudio_ResourceEntry {
    char* key;
    TwinStudio_Resource* value;
} TwinStudio_ResourceEntry;

typedef struct TwinStudio_ResourceRefEntry {
    char* key;
    TwinStudio_Resource** value;
} TwinStudio_ResourceRefEntry;

typedef struct TwinStudio_ResourceDangling {
    TwinStudio_Resource* from;
    const char* key;
    TwinStudio_ResourceRole role;
} TwinStudio_ResourceDangling;

typedef struct TwinStudio_ResourceQueryResult {
    size_t count;
    TwinStudio_Resource** result;
} TwinStudio_ResourceQueryResult;

typedef struct TwinStudio_ResourceDanglingQueryResult {
    size_t count;
    TwinStudio_ResourceDangling* result;
} TwinStudio_ResourceDanglingQueryResult;

typedef struct TwinStudio_ResourceManager {
    TwinStudio_ResourceEntry* resources;
    TwinStudio_ResourceRefEntry* references;
    TwinStudio_Resource**     bucket[TS_RES_TYPE_BITS];
    TwinStudio_Resource**     langBucket[TS_SRT_LangCount];
    TwinStudio_ResourceQueryResult lastQuery;
    TwinStudio_ResourceQueryResult lastDependencyQuery;
    TwinStudio_ResourceDanglingQueryResult danlingResourcesQuery;
    uint32_t                  queryStamp;
} TwinStudio_ResourceManager;

typedef struct TwinStudio_ResourceQuery {
    const char* key;
    TwinStudio_SoundResourceType soundType;
    TwinStudio_ResourceMask mask;
} TwinStudio_ResourceQuery;

typedef struct TwinStudio_ResourceAddRequest {
    const char* key;
    TwinStudio_SoundResourceType soundType;
    TwinStudio_ResourceMask mask;
    TwinStudio_ResourceFree freeFun;
    uint32_t id;
} TwinStudio_ResourceAddRequest;

typedef struct TwinStudio_ResourceLinkRequest {
    const char* key;
    TwinStudio_SoundResourceType soundType;
    TwinStudio_ResourceRole role;
    TwinStudio_Resource* from;
} TwinStudio_ResourceLinkRequest;

typedef enum {
    TS_CP_PS2,
    TS_CP_PS2_DEMO,
    TS_CP_XBOX,
    TS_CP_XBOX_DEMO,

    TS_CP_COUNT
} TwinStudio_ConverterPlatform;

typedef struct TwinStudio_Blob {
    uint8_t* data;
    size_t size;
} TwinStudio_Blob;

void TwinStudio_BlobFree(TwinStudio_Blob* blob);

typedef enum {
    TS_CR_OK,
    TS_CR_ERR_NO_CONVERTER,
    TS_CR_ERR_BAD_DATA,
    TS_CR_ERR_UNSUPPORTED,
    TS_CR_ERR_OUT_OF_MEMORY
} TwinStudio_ConversionResult;

typedef TwinStudio_ConversionResult (*TwinStudio_ResourceImport)(const uint8_t* src, size_t n, void** out);
typedef TwinStudio_ConversionResult (*TwinStudio_ResourceExport)(const void* studioData, TwinStudio_Blob* out);

typedef struct TwinStudio_ResourceConvertRequest {
    union {
        const char* key;
        const TwinStudio_Resource* resource;
    };

    TwinStudio_ResourceMask mask;
    TwinStudio_SoundResourceType soundType;
    TwinStudio_ConverterPlatform platform;
    uint32_t twinId;
    TwinStudio_Blob* importData;
} TwinStudio_ResourceConvertRequest;

typedef struct TwinStudio_ResourceConvertResult {
    TwinStudio_ConversionResult resultCode;
    union {
        TwinStudio_Resource* outRes;
        TwinStudio_Blob* outBlob;
    };
} TwinStudio_ResourceConvertResult;

typedef struct TwinStudio_ResourceConverter {
    TwinStudio_ResourceMask type;
    TwinStudio_ConverterPlatform platform;
    const char* name;

    TwinStudio_ResourceImport importer;
    TwinStudio_ResourceExport exporter;
} TwinStudio_ResourceConverter;

typedef struct TwinStudio_ConvertersStorage {
    const TwinStudio_ResourceConverter* converters[TS_RES_TYPE_BITS][TS_CP_COUNT];
    TwinStudio_ResourceFree             studioFreeFun[TS_RES_TYPE_BITS];
} TwinStudio_ConvertersStorage;

#define TwinStudio_AddOrReplaceResource(data, resKey, twinId, ...) \
    _TwinStudio_AddOrReplaceResource((data), (TwinStudio_ResourceAddRequest) { .soundType = TS_SRT_None, .key = (resKey), .id = (twinId), __VA_ARGS__ })

#define TwinStudio_LinkResource(fromRes, toResKey, linkRole, ...) \
    _TwinStudio_LinkResource((TwinStudio_ResourceLinkRequest) { .soundType = TS_SRT_None, .key = (toResKey), .role = (linkRole), .from = (fromRes), __VA_ARGS__ })

#define TwinStudio_GetResource(resKey, ...) \
    _TwinStudio_GetResource((TwinStudio_ResourceQuery) { .soundType = TS_SRT_None, .key = (resKey), __VA_ARGS__ })

#define TwinStudio_RemoveResource(resKey, ...) \
    _TwinStudio_RemoveResource((TwinStudio_ResourceQuery) { .soundType = TS_SRT_None, .key = (resKey), __VA_ARGS__ })

#define TwinStudio_QueryResources(mask, ...) \
    _TwinStudio_QueryResources((TwinStudio_ResourceQuery) { .soundType = TS_RES_LANG_ANY, .mask = (mask), __VA_ARGS__ })

#define TwinStudio_ResourceImport(resKey, plat, data, out, twinId, ...) \
    _TwinStudio_ResourceImport((TwinStudio_ResourceConvertRequest) { .soundType = TS_SRT_None, .key = (resKey), .platform = (plat), .importData = (data), .outRes = (out), .twinId = (twinId), __VA_ARGS__ })

#define TwinStudio_ResourceExport(res, plat, out, ...) \
    _TwinStudio_ResourceExport((TwinStudio_ResourceConvertRequest) { .resource = (res), .platform = (plat), .outBlob = (out), __VA_ARGS__ })

void TwinStudio_ResourcesInit();
void TwinStudio_ResourcesTerm();
TwinStudio_Resource* _TwinStudio_AddOrReplaceResource(void* data, TwinStudio_ResourceAddRequest request);
int32_t _TwinStudio_LinkResource(TwinStudio_ResourceLinkRequest request);
int32_t TwinStudio_UnlinkResource(TwinStudio_Resource* from, const char* toResKey, TwinStudio_ResourceRole role);
TwinStudio_ResourceLink* TwinStudio_GetResourceLinks(TwinStudio_Resource* res, size_t* amount);
TwinStudio_Resource* TwinStudio_GetLinkTarget(const TwinStudio_ResourceLink* link);
TwinStudio_ResourceQueryResult TwinStudio_GetResourceReferrers(const char* key);
TwinStudio_ResourceQueryResult TwinStudio_GetResourceReferrersByRole(const char* key, TwinStudio_ResourceRole role);
TwinStudio_ResourceQueryResult TwinStudio_GetResourceDependencies(TwinStudio_Resource* root, bool includeRoot);
TwinStudio_ResourceDanglingQueryResult TwinStudio_FindDanglingResources();
size_t TwinStudio_GetReferencesAmount(const char* key);
int32_t _TwinStudio_RemoveResource(TwinStudio_ResourceQuery query);
TwinStudio_Resource* _TwinStudio_GetResource(TwinStudio_ResourceQuery query);
TwinStudio_ResourceQueryResult _TwinStudio_QueryResources(TwinStudio_ResourceQuery query);
TwinStudio_ResourceConvertResult _TwinStudio_ResourceImport(TwinStudio_ResourceConvertRequest request);
TwinStudio_ResourceConvertResult _TwinStudio_ResourceExport(TwinStudio_ResourceConvertRequest request);

void TwinStudio_RegisterConverter(const TwinStudio_ResourceConverter* converter);
void TwinStudio_RegisterConverterType(TwinStudio_ResourceMask type, TwinStudio_ResourceFree freeFun);
const TwinStudio_ResourceConverter* TwinStudio_FindConverter(TwinStudio_ResourceMask type, TwinStudio_ConverterPlatform platform);
TwinStudio_ConversionResult TwinStudio_ConverterImport(TwinStudio_ResourceMask type, TwinStudio_ConverterPlatform platform, TwinStudio_Blob* in, void** out);
TwinStudio_ConversionResult TwinStudio_ConverterExport(TwinStudio_ResourceMask type, TwinStudio_ConverterPlatform platform, const void* studioData, TwinStudio_Blob* out);

#endif // TS_RESOURCES_H