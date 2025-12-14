#ifndef ULIB_H
#define ULIB_H

#include <stddef.h>
#include <stdint.h>

#define FD_STDIN    0
#define FD_STDOUT   1
#define FD_STDERR   2

#define U_GETS_INTR (-2) /* Current line interrupted by Ctrl-C. */

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

/* Simple helpers. */
long u_atol(const char *s);
int u_atoi(const char *s);

/* ===== Tiny stdio based on write() ===== */

int u_putchar(int c);                /* Emit a single character to stdout. */
int u_puts(const char *s);           /* Print string + '\n'. */
int u_printf(const char *fmt, ...);  /* Minimal printf: %s %d %u %x %p %c %% */
int u_snprintf(char *buf, size_t sz, const char *fmt, ...);

/* - u_gets targets interactive terminal input (shell/console only). */
/* - u_read_line is reserved for future network/pipe/file line reading. */
int u_read_line(int fd, char *buf, int buf_size);
int u_getchar(void);                         /* Read one char from stdin. */
int u_gets(char *buf, int buf_size);         /* Read a line from stdin, strip CRLF. */
int u_readn(int fd, void *buf, int nbytes);  /* Read exactly nbytes if possible. */
int u_read_until(int fd, char *buf, int buf_size,
                 char delim);  /* Read until delim or buffer full. */

#endif  /* ULIB_H */
