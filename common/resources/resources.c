#include "resources.h"
#include "memory/memory.h"

#include <assert.h>
#include <stb_ds.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

TwinStudio_ResourceManager resManager;
TwinStudio_ConvertersStorage converters;

static const char* const resLangCode[TS_SRT_LangCount] = { "", "en", "fr", "gr", "sp", "it", "jp" };

static int32_t ctz32(uint32_t x)
{
    int32_t n = 0;
    assert(x != 0);
    while (!(x & 1u))
    {
        x >>= 1;
        ++n;
    }

    return n;
}


void TwinStudio_ResourcesInit()
{
    sh_new_strdup(resManager.resources);
    sh_new_strdup(resManager.references);
}




static char* DuplicateString(const char* str)
{
    size_t n = strlen(str) + 1;
    char* dup = TWIN_MALLOC(n);
    if (dup)
    {
        memcpy(dup, str, n);
    }

    return dup;
}


static void ReferencesAdd(const char* key, TwinStudio_Resource* from)
{
    ptrdiff_t i = shgeti(resManager.references, key);
    if (i < 0)
    {
        shput(resManager.references, (char*)key, NULL);
        i = shgeti(resManager.references, key);
    }

    arrput(resManager.references[i].value, from);
}


static void ReferencesDelete(const char* key, TwinStudio_Resource* from)
{
    ptrdiff_t i = shgeti(resManager.references, key);
    if (i < 0)
    {
        return;
    }

    for (size_t j = 0; j < arrlenu(resManager.references[i].value); ++j)
    {
        if (resManager.references[i].value[j] == from)
        {
            arrdelswap(resManager.references[i].value, j);
            break;
        }
    }

    if (arrlenu(resManager.references[i].value) == 0)
    {
        arrfree(resManager.references[i].value);
        shdel(resManager.references, key);
    }
}


static int32_t ResourceLinkMatches(const TwinStudio_ResourceLink* link, const char* key, TwinStudio_ResourceRole role)
{
    if (role != TS_RR_Any && link->role != role)
    {
        return 0;
    }

    return key == NULL || strcmp(link->key, key) == 0;
}


static TwinStudio_ResourceLink* ResourceFindLink(TwinStudio_Resource* from, const char* toResKey, TwinStudio_ResourceRole role)
{
    for (size_t i = 0; i < arrlenu(from->links); ++i)
    {
        if (ResourceLinkMatches(from->links + i, toResKey, role))
        {
            return from->links + i;
        }
    }

    return NULL;
}


static TwinStudio_Resource* ResourceSlot(TwinStudio_Resource* from, TwinStudio_ResourceRole role)
{
    TwinStudio_ResourceLink* link = ResourceFindLink(from, NULL, role);
    return link ? TwinStudio_GetResource(link->key) : NULL;
}


static void ReferencesWithdraw(TwinStudio_Resource* res)
{
    for (size_t i = 0; i < arrlenu(res->links); ++i)
    {
        ReferencesDelete(res->links[i].key, res);
    }
}


static void BucketPush(TwinStudio_Resource*** resArr, TwinStudio_Resource* res, int32_t slotIndex)
{
    res->slot[slotIndex] = (int32_t)arrlen(*resArr);
    arrput(*resArr, res);
}


static void BucketErase(TwinStudio_Resource*** resArr, TwinStudio_Resource* res, int32_t slotIndex)
{
    size_t i = (size_t)res->slot[slotIndex];
    size_t n = arrlenu(*resArr);

    assert(i < n && (*resArr)[i] == res);

    arrdelswap(*resArr, i);
    if (i < n - 1)
    {
        (*resArr)[i]->slot[slotIndex] = (int32_t)i;
    }
    res->slot[slotIndex] = -1;
}


static void ResourceDestroy(TwinStudio_Resource* res)
{
    for (size_t i = 0; i < arrlenu(res->links); ++i)
    {
        TWIN_FREE(res->links[i].key);
    }
    arrfree(res->links);

    if (res->freeFun && res->data)
    {
        res->freeFun(res->data);
    }

    TWIN_FREE(res);
}


static const char* ResourceStorageKey(const char* key, TwinStudio_SoundResourceType soundLang, char* buf, size_t cap)
{
    if (soundLang == TS_SRT_None)
    {
        return key;
    }

    assert(strchr(key, TS_RES_LANG_SEP) == NULL);
    int32_t n = snprintf(buf, cap, "%s%c%s", key, TS_RES_LANG_SEP, resLangCode[soundLang]);
    assert(n > 0 && (size_t)n < cap);
    return buf;
}


static TwinStudio_Resource* GetResourceByKey(const char* key)
{
    ptrdiff_t i = shgeti(resManager.resources, key);
    return (i >= 0) ? resManager.resources[i].value : NULL;
}


static int32_t RemoveResourceByKey(const char* key)
{
    ptrdiff_t i = shgeti(resManager.resources, key);
    if (i < 0)
    {
        return 0;
    }

    TwinStudio_Resource* res = resManager.resources[i].value;
    ReferencesWithdraw(res);
    for (TwinStudio_ResourceMask rest = res->mask; rest; rest &= rest - 1)
    {
        BucketErase(&resManager.bucket[ctz32(rest)], res, ctz32(rest));
    }
    BucketErase(&resManager.langBucket[res->soundType], res, TS_RES_SLOT_LANG);

    shdel(resManager.resources, key);
    ResourceDestroy(res);

    return 1;
}


static int32_t LinkResourceByKey(TwinStudio_Resource* from, const char* key, TwinStudio_ResourceRole role)
{
    TwinStudio_ResourceLink link = { 0 };
    assert(role != TS_RR_Any);

    if (!from || !key)
    {
        return 0;
    }
    link.key = DuplicateString(key);
    if (!link.key)
    {
        return 0;
    }
    link.role = role;

    arrput(from->links, link);
    ReferencesAdd(key, from);

    return 1;
}


int32_t _TwinStudio_LinkResource(TwinStudio_ResourceLinkRequest request)
{
    if (request.soundType == TS_SRT_None)
    {
        return LinkResourceByKey(request.from, request.key, request.role);
    }

    char buf[TS_RES_KEY_MAX];
    return LinkResourceByKey(request.from, ResourceStorageKey(request.key, request.soundType, buf, sizeof(buf)), request.role);
}


int32_t TwinStudio_UnlinkResource(TwinStudio_Resource* from, const char* toResKey, TwinStudio_ResourceRole role)
{
    for (size_t i = 0; i < arrlenu(from->links); ++i)
    {
        if (!ResourceLinkMatches(from->links + i, toResKey, role))
        {
            continue;
        }

        TWIN_FREE(from->links[i].key);
        arrdelswap(from->links, i);
        ReferencesDelete(toResKey, from);
        return 1;
    }

    return 0;
}


TwinStudio_ResourceLink* TwinStudio_GetResourceLinks(TwinStudio_Resource* res, size_t* amount)
{
    if (amount)
    {
        *amount = arrlenu(res->links);
    }
    return res->links;
}


TwinStudio_Resource* TwinStudio_GetLinkTarget(const TwinStudio_ResourceLink* link)
{
    return TwinStudio_GetResource(link->key);
}


TwinStudio_ResourceQueryResult TwinStudio_GetResourceReferrers(const char* key)
{
    TwinStudio_ResourceQueryResult result = { 0 };

    ptrdiff_t i = shgeti(resManager.references, key);
    if (i < 0)
    {
        return result;
    }
    
    result.count = arrlenu(resManager.references[i].value);
    result.result = resManager.references[i].value;
    return result;
}


size_t TwinStudio_GetReferencesAmount(const char* key)
{
    TwinStudio_ResourceQueryResult result = TwinStudio_GetResourceReferrers(key);
    return result.count;
}


TwinStudio_ResourceQueryResult TwinStudio_GetResourceReferrersByRole(const char* key, TwinStudio_ResourceRole role)
{
    size_t n = 0;
    TwinStudio_ResourceQueryResult all = TwinStudio_GetResourceReferrers(key);
    size_t nAll = all.count;
    uint32_t stamp = resManager.queryStamp++;

    if (resManager.lastQuery.result)
    {
        arrsetlen(resManager.lastQuery.result, arrlenu(resManager.lastQuery.result) * 0);
    }

    for (size_t i = 0; i < nAll; ++i)
    {
        TwinStudio_Resource* res = all.result[i];
        if (res->queryStamp == stamp)
        {
            continue;
        }

        if (!ResourceFindLink(res, key, role))
        {
            continue;
        }

        res->queryStamp = stamp;
        arrput(resManager.lastQuery.result, res);
        ++n;
    }

    resManager.lastQuery.count = n;
    return resManager.lastQuery;
}


TwinStudio_ResourceQueryResult TwinStudio_GetResourceDependencies(TwinStudio_Resource* root, bool includeRoot)
{
    TwinStudio_Resource** stack = NULL;
    uint32_t stamp = resManager.queryStamp++;
    size_t n = 0;

    if (resManager.lastDependencyQuery.result)
    {
        arrsetlen(resManager.lastDependencyQuery.result, arrlenu(resManager.lastDependencyQuery.result) * 0);
    }

    root->queryStamp = stamp;
    arrput(stack, root);
    if (includeRoot)
    {
        arrput(resManager.lastDependencyQuery.result, root);
        n++;
    }

    while (arrlenu(stack))
    {
        TwinStudio_Resource* res = arrpop(stack);
        for (size_t i = 0; i < arrlenu(res->links); ++i)
        {
            TwinStudio_Resource* linkRes = TwinStudio_GetResource(res->links[i].key);
            if (!linkRes || linkRes->queryStamp == stamp)
            {
                continue;
            }

            linkRes->queryStamp = stamp;
            arrput(resManager.lastDependencyQuery.result, linkRes);
            n++;
            arrput(stack, linkRes);
        }
    }
    arrfree(stack);

    resManager.lastDependencyQuery.count = n;
    return resManager.lastDependencyQuery;
}


TwinStudio_ResourceDanglingQueryResult TwinStudio_FindDanglingResources()
{
    size_t n = 0;

    if (resManager.danlingResourcesQuery.result)
    {
        arrsetlen(resManager.danlingResourcesQuery.result, arrlenu(resManager.danlingResourcesQuery.result) * 0);
    }

    for (size_t i = 0; i < shlenu(resManager.resources); ++i)
    {
        TwinStudio_Resource* res = resManager.resources[i].value;
        for (size_t j = 0; j < arrlenu(res->links); ++j)
        {
            if (TwinStudio_GetResource(res->links[j].key))
            {
                continue;
            }
            TwinStudio_ResourceDangling dangle = { .from = res, .key = res->links[j].key, .role = res->links[j].role };
            arrput(resManager.danlingResourcesQuery.result, dangle);
            n++;
        }
    }

    resManager.danlingResourcesQuery.count = n;
    return resManager.danlingResourcesQuery;
}


TwinStudio_Resource* _TwinStudio_AddOrReplaceResource(void* data, TwinStudio_ResourceAddRequest query)
{
    assert(query.mask != 0 && (query.mask & ~(TwinStudio_ResourceMask)TS_RT_ALL) == 0);
    assert(query.soundType >= TS_SRT_None && query.soundType < TS_SRT_LangCount);

    if (query.soundType != TS_SRT_None)
    {
        query.mask |= TS_RT_Localized;
    }

    char buf[TS_RES_KEY_MAX];
    const char *skey = ResourceStorageKey(query.key, query.soundType, buf, sizeof(buf));
    TwinStudio_RemoveResource(skey);

    TwinStudio_Resource* res = TWIN_MALLOC(sizeof *res);
    if (!res)
    {
        return NULL;
    }

    res->id = query.id;
    res->mask = query.mask;
    res->soundType = query.soundType;
    res->data = data;
    res->freeFun = query.freeFun;
    for (int32_t i = 0; i <= TS_RES_TYPE_BITS; ++i)
    {
        res->slot[i] = -1;
    }

    shput(resManager.resources, (char*)skey, res);
    res->key = resManager.resources[shgeti(resManager.resources, skey)].key;

    for (TwinStudio_ResourceMask rest = query.mask; rest; rest &= rest - 1)
    {
        BucketPush(&resManager.bucket[ctz32(rest)], res, ctz32(rest));
    }
    BucketPush(&resManager.langBucket[query.soundType], res, TS_RES_SLOT_LANG);

    return res;
}


int32_t _TwinStudio_RemoveResource(TwinStudio_ResourceQuery query)
{
    if (query.soundType == TS_SRT_None)
    {
        return RemoveResourceByKey(query.key);
    }

    char buf[TS_RES_KEY_MAX];
    return RemoveResourceByKey(ResourceStorageKey(query.key, query.soundType, buf, sizeof(buf)));
}


TwinStudio_Resource* _TwinStudio_GetResource(TwinStudio_ResourceQuery query)
{
    if (query.soundType == TS_SRT_None)
    {
        return GetResourceByKey(query.key);
    }

    char buf[TS_RES_KEY_MAX];
    return GetResourceByKey(ResourceStorageKey(query.key, query.soundType, buf, sizeof(buf)));
}


static int32_t ResourceMatches(const TwinStudio_Resource* res, TwinStudio_ResourceMask mask)
{
    return (res->mask & mask) != 0;
}


TwinStudio_ResourceQueryResult _TwinStudio_QueryResources(TwinStudio_ResourceQuery query)
{
    size_t n = 0;

    query.mask &= (TwinStudio_ResourceMask)TS_RT_ALL;
    if (query.mask == 0)
    {
        query.mask = TS_RT_ALL;
    }
    assert(query.soundType == TS_RES_LANG_ANY || (query.soundType >= 0 && query.soundType < TS_SRT_LangCount));

    if (query.mask == TS_RT_ALL && query.soundType != TS_RES_LANG_ANY)
    {
        return (TwinStudio_ResourceQueryResult) { .count = arrlenu(resManager.langBucket[query.soundType]), .result = resManager.langBucket[query.soundType] };
    }

    if (query.soundType == TS_RES_LANG_ANY && (query.mask & (query.mask - 1)) == 0)
    {
        int32_t b = ctz32(query.mask);
        return (TwinStudio_ResourceQueryResult) { .count = arrlenu(resManager.bucket[n]), .result = resManager.bucket[b] };
    }

    if (resManager.lastQuery.result)
    {
        arrsetlen(resManager.lastQuery.result, arrlenu(resManager.lastQuery.result) * 0);
    }

    TwinStudio_Resource** candidates = NULL;
    size_t candidatesAmt = 0;
    int32_t haveCandidate = 0;

    if (query.soundType != TS_RES_LANG_ANY)
    {
        candidates = resManager.langBucket[query.soundType];
        candidatesAmt = arrlenu(candidates);
        haveCandidate = 1;
    }

    if (haveCandidate)
    {
        for (size_t i = 0; i < candidatesAmt; ++i)
        {
            TwinStudio_Resource* res = candidates[i];
            if (ResourceMatches(res, query.mask))
            {
                arrput(resManager.lastQuery.result, res);
                ++n;
            }
        }
    }
    else
    {
        uint32_t s = ++resManager.queryStamp;
        for (TwinStudio_ResourceMask rest = query.mask; rest; rest &= rest - 1)
        {
            int32_t b = ctz32(rest);
            for (size_t i = 0; i < arrlenu(resManager.bucket[b]); ++i)
            {
                TwinStudio_Resource* res = resManager.bucket[b][i];
                if (res->queryStamp != s)
                {
                    res->queryStamp = s;
                    arrput(resManager.lastQuery.result, res);
                    ++n;
                }
            }
        }
    }

    resManager.lastQuery.count = n;
    return resManager.lastQuery;
}

static TwinStudio_ResourceMask ConverterResolveType(TwinStudio_ResourceMask mask, TwinStudio_ConverterPlatform platform)
{
    TwinStudio_ResourceMask found = 0;
    for (TwinStudio_ResourceMask rest = mask; rest; rest &= rest - 1) {
        TwinStudio_ResourceMask bit = rest & ~(rest - 1);
        if (TwinStudio_FindConverter(bit, platform)) {
            assert(!found);
            found = bit;
        }
    }

    return found;
}

TwinStudio_ResourceConvertResult _TwinStudio_ResourceImport(TwinStudio_ResourceConvertRequest request)
{
    TwinStudio_ResourceConvertResult result = { 0 };
    void* studioRes = NULL;

    TwinStudio_ResourceMask type = ConverterResolveType(request.mask, request.platform);
    result.outRes = NULL;
    if (!type)
    {
        result.resultCode = TS_CR_ERR_NO_CONVERTER;
        return result;
    }

    TwinStudio_ConversionResult resultCode = TwinStudio_ConverterImport(type, request.platform, request.importData, &studioRes);
    if (resultCode != TS_CR_OK)
    {
        result.resultCode = resultCode;
        return result;
    }

    result.outRes = TwinStudio_AddOrReplaceResource(studioRes, request.key, request.twinId, .freeFun = converters.studioFreeFun[ctz32(type)], .soundType = request.soundType);
    if (!result.outRes)
    {
        converters.studioFreeFun[ctz32(type)](studioRes);
        result.resultCode = TS_CR_ERR_OUT_OF_MEMORY;
        return result;
    }

    result.outRes->kind = type;
    result.resultCode = TS_CR_OK;
    return result;
}

TwinStudio_ResourceConvertResult _TwinStudio_ResourceExport(TwinStudio_ResourceConvertRequest request)
{
    TwinStudio_ResourceConvertResult result = { 0 };
    TwinStudio_ResourceMask type = request.resource->kind ? request.resource->kind : ConverterResolveType(request.resource->mask, request.platform);
    result.outBlob = NULL;
    if (!type)
    {
        result.resultCode = TS_CR_ERR_NO_CONVERTER;
        return result;
    }
    
    result.resultCode = TwinStudio_ConverterExport(type, request.platform, request.resource->data, result.outBlob);
    return result;
}

void TwinStudio_RegisterConverter(const TwinStudio_ResourceConverter* converter)
{
    assert(converter->type && (converter->type & (converter->type - 1)) == 0);
    assert(converter->platform >= 0 && converter->platform < TS_CP_COUNT);
    assert(converters.studioFreeFun[ctz32(converter->type)]);
    converters.converters[ctz32(converter->type)][converter->platform] = converter;
}

void TwinStudio_RegisterConverterType(TwinStudio_ResourceMask type, TwinStudio_ResourceFree freeFun)
{
    assert(type && (type & (type - 1)) == 0);
    converters.studioFreeFun[ctz32(type)] = freeFun;
}

const TwinStudio_ResourceConverter* TwinStudio_FindConverter(TwinStudio_ResourceMask type, TwinStudio_ConverterPlatform platform)
{
    if (!type || (type & (type - 1)) != 0)
    {
        return NULL;
    }
    if (platform < 0 || platform >= TS_CP_COUNT)
    {
        return NULL;
    }
    return converters.converters[ctz32(type)][platform];
}

TwinStudio_ConversionResult TwinStudio_ConverterImport(TwinStudio_ResourceMask type, TwinStudio_ConverterPlatform platform, TwinStudio_Blob* in, void** out)
{
    const TwinStudio_ResourceConverter* converter = TwinStudio_FindConverter(type, platform);
    *out = NULL;
    if (!converter || !converter->importer)
    {
        return TS_CR_ERR_UNSUPPORTED;
    }
    return converter->importer(in->data, in->size, out);
}

TwinStudio_ConversionResult TwinStudio_ConverterExport(TwinStudio_ResourceMask type, TwinStudio_ConverterPlatform platform, const void* studioData, TwinStudio_Blob* out)
{
    const TwinStudio_ResourceConverter* converter = TwinStudio_FindConverter(type, platform);
    out->data = NULL;
    out->size = 0;
    if (!converter || !converter->exporter)
    {
        return TS_CR_ERR_UNSUPPORTED;
    }
    return converter->exporter(studioData, out);
}


void TwinStudio_ResourcesTerm()
{
    for (size_t i = 0; i < shlenu(resManager.resources); ++i)
    {
        ResourceDestroy(resManager.resources[i].value);
    }

    for (size_t i = 0; i < shlenu(resManager.references); ++i)
    {
        arrfree(resManager.references[i].value);
    }

    for (int32_t b = 0; b < TS_RES_TYPE_BITS; ++b)
    {
        arrfree(resManager.bucket[b]);
    }

    for (int32_t l = 0; l < TS_SRT_LangCount; ++l)
    {
        arrfree(resManager.langBucket[l]);
    }

    if (resManager.lastQuery.result)
    {
        arrfree(resManager.lastQuery.result);
        resManager.lastQuery.result = NULL;
        resManager.lastQuery.count = 0;
    }
    if (resManager.lastDependencyQuery.result)
    {
        arrfree(resManager.lastDependencyQuery.result);
        resManager.lastDependencyQuery.result = NULL;
        resManager.lastDependencyQuery.count = 0;
    }
    if (resManager.danlingResourcesQuery.result)
    {
        arrfree(resManager.danlingResourcesQuery.result);
        resManager.danlingResourcesQuery.result = NULL;
        resManager.danlingResourcesQuery.count = 0;
    }
    shfree(resManager.references);
    shfree(resManager.resources);
}
