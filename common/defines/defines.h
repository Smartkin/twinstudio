#ifndef TS_DEFINES_H
#define TS_DEFINES_H


#if defined(_MSC_VER) && !defined(__clang__)
#define TS_COMPACT_STRUCT __pragma(pack(push, 1)) struct __pragma(pack(pop))
#else
#define TS_COMPACT_STRUCT struct __attribute__((__packed__))
#endif


#if defined(_MSC_VER) && !defined(__clang__)
#define TS_COMPACT_UNION __pragma(pack(push, 1)) union __pragma(pack(pop))
#else
#define TS_COMPACT_UNION union __attribute__((__packed__))
#endif

#if defined(_MSC_VER) && !defined(__clang__)
#define TS_COMPACT_ENUM __pragma(pack(push, 1)) enum __pragma(pack(pop))
#else
#define TS_COMPACT_ENUM enum __attribute__((__packed__))
#endif


#define TS_RESOURCE_HANDLER_DECL(extension) void HandleResource##extension(TwinStudio_BinarySerializer* deserializer, TwinStudio_Arena* arena, size_t size);
#define TS_RESOURCE_HANDLER_STUB(extension) void HandleResource##extension(TwinStudio_BinarySerializer* deserializer, TwinStudio_Arena* arena, size_t size) { TwinStudio_BinReadVoid(deserializer, size); }
#define TS_RESOURCE_HANDLER(extension, deserializer, arena, size) HandleResource##extension((deserializer), (arena), (size));


#endif // TS_DEFINES_H