/* SPDX-License-Identifier: GPL-2.0-only */
#include <stddef.h>

void *memset(void *target, int value, size_t size)
{
    unsigned char *p = target;
    for (size_t i = 0; i < size; ++i) p[i] = (unsigned char)value;
    return target;
}

void *memcpy(void *restrict target, const void *restrict source, size_t size)
{
    unsigned char *to = target;
    const unsigned char *from = source;
    for (size_t i = 0; i < size; ++i) to[i] = from[i];
    return target;
}

void *memmove(void *target, const void *source, size_t size)
{
    unsigned char *to = target;
    const unsigned char *from = source;
    if ((size_t)to < (size_t)from) {
        for (size_t i = 0; i < size; ++i) to[i] = from[i];
    } else {
        while (size) { --size; to[size] = from[size]; }
    }
    return target;
}
