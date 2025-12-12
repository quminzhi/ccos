#include "ulib.h"

/* ===== memory ===== */

void *u_memcpy(void *dst, const void *src, size_t n)
{
  unsigned char *d       = (unsigned char *)dst;
  const unsigned char *s = (const unsigned char *)src;

  for (size_t i = 0; i < n; ++i) {
    d[i] = s[i];
  }
  return dst;
}

/* Supports overlapping regions. */
void *u_memmove(void *dst, const void *src, size_t n)
{
  unsigned char *d       = (unsigned char *)dst;
  const unsigned char *s = (const unsigned char *)src;

  if (d == s || n == 0) {
    return dst;
  }

  if (d < s) {
    /* Forward copy. */
    for (size_t i = 0; i < n; ++i) {
      d[i] = s[i];
    }
  } else {
    /* Reverse copy to avoid overwrite. */
    for (size_t i = n; i > 0; --i) {
      d[i - 1] = s[i - 1];
    }
  }
  return dst;
}

void *u_memset(void *s, int c, size_t n)
{
  unsigned char *p = (unsigned char *)s;
  unsigned char v  = (unsigned char)c;

  for (size_t i = 0; i < n; ++i) {
    p[i] = v;
  }
  return s;
}

int u_memcmp(const void *s1, const void *s2, size_t n)
{
  const unsigned char *a = (const unsigned char *)s1;
  const unsigned char *b = (const unsigned char *)s2;

  for (size_t i = 0; i < n; ++i) {
    if (a[i] != b[i]) {
      return (int)a[i] - (int)b[i];
    }
  }
  return 0;
}

/* ===== string ===== */

size_t u_strlen(const char *s)
{
  size_t n = 0;
  while (s[n] != '\0') {
    ++n;
  }
  return n;
}

int u_strcmp(const char *a, const char *b)
{
  while (*a && (*a == *b)) {
    ++a;
    ++b;
  }
  return (unsigned char)*a - (unsigned char)*b;
}

int u_strncmp(const char *a, const char *b, size_t n)
{
  for (size_t i = 0; i < n; ++i) {
    unsigned char ca = (unsigned char)a[i];
    unsigned char cb = (unsigned char)b[i];

    if (ca != cb) {
      return (int)ca - (int)cb;
    }
    if (ca == '\0') {
      return 0;
    }
  }
  return 0;
}

char *u_strcpy(char *dst, const char *src)
{
  char *ret = dst;
  while ((*dst++ = *src++) != '\0') {
    /* empty */
  }
  return ret;
}

/* Copy up to n bytes, padding with zeros if needed. */
char *u_strncpy(char *dst, const char *src, size_t n)
{
  char *ret = dst;
  size_t i  = 0;

  for (; i < n && src[i] != '\0'; ++i) {
    dst[i] = src[i];
  }
  for (; i < n; ++i) {
    dst[i] = '\0';
  }
  return ret;
}

char *u_strchr(const char *s, int c)
{
  char ch = (char)c;

  while (*s) {
    if (*s == ch) {
      return (char *)s;
    }
    ++s;
  }
  return (ch == '\0') ? (char *)s : NULL;
}

char *u_strrchr(const char *s, int c)
{
  char ch          = (char)c;
  const char *last = NULL;

  while (*s) {
    if (*s == ch) {
      last = s;
    }
    ++s;
  }
  if (ch == '\0') {
    return (char *)s;
  }
  return (char *)last;
}

/* ===== Simple number helpers ===== */

static int u_isspace_ch(char c)
{
  return (c == ' ') || (c == '\t') || (c == '\n') || (c == '\r') ||
         (c == '\f') || (c == '\v');
}

static int u_isdigit_ch(char c) { return (c >= '0') && (c <= '9'); }

long u_atol(const char *s)
{
  long sign = 1;
  long val  = 0;

  /* Skip leading whitespace. */
  while (u_isspace_ch(*s)) {
    ++s;
  }

  if (*s == '+') {
    ++s;
  } else if (*s == '-') {
    sign = -1;
    ++s;
  }

  while (u_isdigit_ch(*s)) {
    val = val * 10 + (*s - '0');
    ++s;
  }

  return sign * val;
}

int u_atoi(const char *s) { return (int)u_atol(s); }
