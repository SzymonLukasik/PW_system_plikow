#ifndef SAFEALLOC_H
#define SAFEALLOC_H

#include <stdlib.h>

void* safe_malloc(size_t size);

void* safe_realloc(void* ptr, size_t size);

void* safe_calloc(size_t n, size_t size);

#endif /* SAFEALLOC_H */