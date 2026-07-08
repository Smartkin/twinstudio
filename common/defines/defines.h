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


#endif // TS_DEFINES_H