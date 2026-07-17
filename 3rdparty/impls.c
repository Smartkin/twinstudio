// Put the implementations of single header libraries here to not think where we need to define their implementations

#define CLAY_IMPLEMENTATION
#include <clay.h>
#undef CLAY_IMPLEMENTATION

#define STB_DS_IMPLEMENTATION
#include <rpmalloc.h>
#define STBDS_REALLOC(c,p,s) rprealloc(p,s)
#define STBDS_FREE(c,p)      rpfree(p)
#include <stb_ds.h>
#undef STB_DS_IMPLEMENTATION