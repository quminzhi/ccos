// klib.h - small C runtime helpers for bare-metal

#ifndef KLIB_H
#define KLIB_H

#include <stddef.h>  // for size_t; freestanding 环境里这个头是标准的一部分

// Set the first n bytes of the block of memory pointed by s
// to the specified value (interpreted as unsigned char).
void *memset(void *s, int c, size_t n);

// Copy n bytes from memory area src to memory area dest.
// The memory areas must not overlap.
void *memcpy(void *dest, const void *src, size_t n);

// Compare the first n bytes of s1 and s2.
// Returns:
//   < 0  if s1 < s2
//   = 0  if s1 == s2
//   > 0  if s1 > s2
int memcmp(const void *s1, const void *s2, size_t n);

size_t strlen(const char *s);

#endif /* KLIB_H */
