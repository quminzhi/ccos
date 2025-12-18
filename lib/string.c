#include "string.h"

void *memmove(void *dest, const void *src, size_t n)
{
  unsigned char *d       = (unsigned char *)dest;
  const unsigned char *s = (const unsigned char *)src;

  if (d == s || n == 0) {
    return dest;
  }

  if (d < s) {
    // Forward copy
    for (size_t i = 0; i < n; i++) {
      d[i] = s[i];
    }
  } else {
    // Backward copy (for overlapping regions)
    for (size_t i = n; i > 0; i--) {
      d[i - 1] = s[i - 1];
    }
  }

  return dest;
}

void *memchr(const void *s, int c, size_t n)
{
  const unsigned char *p = (const unsigned char *)s;
  unsigned char uc       = (unsigned char)c;

  for (size_t i = 0; i < n; i++) {
    if (p[i] == uc) {
      return (void *)(p + i);
    }
  }
  return NULL;
}

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

char *strchr(const char *s, int c)
{
  char ch = (char)c;
  for (; *s; s++) {
    if (*s == ch) return (char *)s;
  }
  return (ch == '\0') ? (char *)s : NULL;
}

char *strrchr(const char *s, int c)
{
  char ch          = (char)c;
  const char *last = NULL;

  for (; *s; s++) {
    if (*s == ch) {
      last = s;
    }
  }
  if (ch == '\0') {
    return (char *)s;  // Points to the trailing '\0'
  }
  return (char *)last;
}

size_t strnlen(const char *s, size_t maxlen)
{
  size_t i = 0;
  for (; i < maxlen && s[i] != '\0'; i++) {
    /* nothing */
  }
  return i;
}
