#ifndef ULIB_H
#define ULIB_H

#include <stddef.h>
#include <stdint.h>

/* ===== memory ===== */

void *u_memcpy(void *dst, const void *src, size_t n);
void *u_memmove(void *dst, const void *src, size_t n);
void *u_memset(void *s, int c, size_t n);
int u_memcmp(const void *s1, const void *s2, size_t n);

/* ===== string ===== */

size_t u_strlen(const char *s);
int u_strcmp(const char *a, const char *b);
int u_strncmp(const char *a, const char *b, size_t n);
char *u_strcpy(char *dst, const char *src);
char *u_strncpy(char *dst, const char *src, size_t n);
char *u_strchr(const char *s, int c);
char *u_strrchr(const char *s, int c);

/* 简单工具 */
long u_atol(const char *s);
int u_atoi(const char *s);

/* ===== tiny stdio (基于 write) ===== */

int u_putchar(int c);                // 输出单个字符到 stdout
int u_puts(const char *s);           // 输出字符串 + '\n'
int u_printf(const char *fmt, ...);  // 最小 printf: %s %d %u %x %p %c %%
int u_snprintf(char *buf, size_t sz, const char *fmt, ...);

#endif  // ULIB_H
