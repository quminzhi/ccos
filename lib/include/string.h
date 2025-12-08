// string.h - minimal C string/memory declarations for bare-metal

#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void *memset(void *s, int c, size_t n);
void *memcpy(void *dest, const void *src, size_t n);
int   memcmp(const void *s1, const void *s2, size_t n);
size_t strlen(const char *s);

// 为 libfdt 新增的几个
void *memmove(void *dest, const void *src, size_t n);
void *memchr(const void *s, int c, size_t n);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
size_t strnlen(const char *s, size_t maxlen);

#ifdef __cplusplus
}
#endif
