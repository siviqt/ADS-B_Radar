#pragma once
#include <cstdlib>
#define MALLOC_CAP_SPIRAM 0
inline void *heap_caps_calloc(size_t n,size_t size,int) {return calloc(n,size);}
