#ifndef ULIB_H
#define ULIB_H

#include <stddef.h>
#include <stdint.h>

#define FD_STDIN  0
#define FD_STDOUT 1
#define FD_STDERR 2

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

// - u_gets 定义为“终端交互用的行读取”，只在 shell/console 用
// - u_read_line 保留给将来做网络 / 管道 / 文件行读取时用
int u_read_line(int fd, char *buf, int buf_size);
int u_getchar(void);                         // 从 stdin 读一个字符
int u_gets(char *buf, int buf_size);         // 从 stdin 读一行，去掉行尾 \r\n
int u_readn(int fd, void *buf, int nbytes);  // 试图读满 nbytes
int u_read_until(int fd, char *buf, int buf_size,
                 char delim);  // 一直读到 delim 或 buffer 满

#endif  // ULIB_H
