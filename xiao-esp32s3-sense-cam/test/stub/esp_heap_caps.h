/* Host stand-in (test only): one heap. */
#pragma once
#include <stdlib.h>
#define MALLOC_CAP_8BIT     (1u << 2)
#define MALLOC_CAP_SPIRAM   (1u << 10)
#define MALLOC_CAP_INTERNAL (1u << 11)
static inline void *heap_caps_malloc(size_t size, unsigned caps) { (void)caps; return malloc(size); }
static inline size_t heap_caps_get_free_size(unsigned caps) { (void)caps; return 0; }
