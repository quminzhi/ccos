// klib.c - small C runtime helpers for bare-metal

#include "klib.h"

void *memset(void *s, int c, size_t n)
{
  unsigned char *p    = (unsigned char *)s;
  unsigned char value = (unsigned char)c;

  for (size_t i = 0; i < n; ++i) {
    p[i] = value;
  }

  return s;
}

void *memcpy(void *dest, const void *src, size_t n)
{
  unsigned char *d       = (unsigned char *)dest;
  const unsigned char *s = (const unsigned char *)src;

  for (size_t i = 0; i < n; ++i) {
    d[i] = s[i];
  }

  return dest;
}

int memcmp(const void *s1, const void *s2, size_t n)
{
  const unsigned char *p1 = (const unsigned char *)s1;
  const unsigned char *p2 = (const unsigned char *)s2;

  for (size_t i = 0; i < n; ++i) {
    if (p1[i] != p2[i]) {
      // cast to int to avoid unsigned wrap confusion
      return (int)p1[i] - (int)p2[i];
    }
  }

  return 0;
}

size_t strlen(const char *s)
{
  size_t n = 0;
  while (s[n] != '\0') {
    n++;
  }
  return n;
}
