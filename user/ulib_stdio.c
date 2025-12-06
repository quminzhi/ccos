#include "ulib.h"
#include "syscall.h"
#include <stdarg.h>

/* 内部 util: 输出一个缓冲区到 stdout */
static int write_all(const char *buf, size_t len)
{
  /* 对于简单系统，假设一次 sys_write 就能写完 */
  int rc = write(1, buf, len);
  return rc;
}

int u_putchar(int c)
{
  char ch = (char)c;
  int rc  = write_all(&ch, 1);
  return (rc == 1) ? c : -1;
}

int u_puts(const char *s)
{
  size_t len = u_strlen(s);
  int rc1    = write_all(s, len);
  int rc2    = write_all("\n", 1);
  if (rc1 < 0 || rc2 < 0) {
    return -1;
  }
  return (int)(len + 1);
}

/* 把整数转成字符串（radix = 10 or 16） */
static char *u_itoa(long long value, char *buf_end, int base, int sign)
{
  /* buf_end 指向缓冲区末尾后一个位置，从后往前写 */
  unsigned long long v;

  if (sign && value < 0) {
    v = (unsigned long long)(-value);
  } else {
    v = (unsigned long long)value;
  }

  char *p = buf_end;
  if (v == 0) {
    *--p = '0';
  } else {
    while (v > 0) {
      unsigned digit = (unsigned)(v % (unsigned)base);
      v /= (unsigned)base;
      if (digit < 10) {
        *--p = (char)('0' + digit);
      } else {
        *--p = (char)('a' + (digit - 10));
      }
    }
  }

  if (sign && value < 0) {
    *--p = '-';
  }

  return p;
}

static int u_vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
  size_t total = 0;  // 实际“应该输出”的字符数（不含结尾 '\0'）
  size_t pos   = 0;  // 当前写入位置索引（0..total）
  size_t avail = 0;  // 可真正写入的最大字符数（预留 1 个字节给 '\0'）

  if (size > 0) {
    avail = size - 1;  // 最多写 size-1 个可见字符
  }

  for (; *fmt; ++fmt) {
    if (*fmt != '%') {
      if (pos < avail) {
        buf[pos] = *fmt;
      }
      ++pos;
      ++total;
      continue;
    }

    ++fmt;
    if (*fmt == '\0') {
      break;
    }

    if (*fmt == '%') {
      if (pos < avail) {
        buf[pos] = '%';
      }
      ++pos;
      ++total;
      continue;
    }

    char tmp[64];
    char *str  = tmp;
    size_t len = 0;

    switch (*fmt) {
      case 'c': {
        int c  = va_arg(ap, int);
        tmp[0] = (char)c;
        len    = 1;
        str    = tmp;
        break;
      }
      case 's': {
        const char *s = va_arg(ap, const char *);
        if (!s) s = "(null)";
        str = (char *)s;
        len = u_strlen(s);
        break;
      }
      case 'd':
      case 'i': {
        int v       = va_arg(ap, int);
        char *start = u_itoa((long long)v, tmp + sizeof(tmp), 10, 1);
        str         = start;
        len         = (size_t)((tmp + sizeof(tmp)) - start);
        break;
      }
      case 'u': {
        unsigned int v = va_arg(ap, unsigned int);
        char *start    = u_itoa((long long)v, tmp + sizeof(tmp), 10, 0);
        str            = start;
        len            = (size_t)((tmp + sizeof(tmp)) - start);
        break;
      }
      case 'x':
      case 'X': {
        unsigned int v = va_arg(ap, unsigned int);
        char *start    = u_itoa((long long)v, tmp + sizeof(tmp), 16, 0);
        str            = start;
        len            = (size_t)((tmp + sizeof(tmp)) - start);
        break;
      }
      case 'p': {
        uintptr_t v = (uintptr_t)va_arg(ap, void *);
        char *start = u_itoa((long long)v, tmp + sizeof(tmp), 16, 0);
        *--start    = 'x';
        *--start    = '0';
        str         = start;
        len         = (size_t)((tmp + sizeof(tmp)) - start);
        break;
      }
      default:
        // 不支持的格式，输出 "%?"
        if (pos < avail) buf[pos] = '%';
        ++pos;
        ++total;

        if (pos < avail) buf[pos] = *fmt;
        ++pos;
        ++total;
        continue;
    }

    for (size_t i = 0; i < len; ++i) {
      if (pos < avail) {
        buf[pos] = str[i];
      }
      ++pos;
      ++total;
    }
  }

  // 确保以 '\0' 结束（如果 size > 0）
  if (size > 0) {
    size_t term = (pos <= avail) ? pos : avail;
    buf[term]   = '\0';
  }

  return (int)total;
}

int u_snprintf(char *buf, size_t size, const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  int n = u_vsnprintf(buf, size, fmt, ap);
  va_end(ap);
  return n;
}

int u_printf(const char *fmt, ...)
{
  char buf[256];

  va_list ap;
  va_start(ap, fmt);
  int n = u_vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);

  if (n > 0) {
    /* u_vsnprintf 返回的是“理论长度”，可能超过 buf 大小 */
    size_t to_write = (n < (int)sizeof(buf)) ? (size_t)n : sizeof(buf) - 1;
    write_all(buf, to_write);
  }
  return n;
}

int u_read_line(int fd, char *buf, int buf_size)
{
  if (buf_size <= 1) {
    return -1;
  }

  int used = 0;

  for (;;) {
    if (used >= buf_size - 1) {
      // 缓冲区满了，直接结束这一行
      break;
    }

    char c;
    int n = read(fd, &c, 1);
    if (n < 0) {
      // 未来有错误码可以直接返回 n
      return n;
    }
    if (n == 0) {
      // EOF
      if (used == 0) {
        return 0;
      }
      break;
    }

    if (c == '\n' || c == '\r') {
      // 行结束，丢掉行尾，不写入 buf
      break;
    }

    buf[used++] = c;
  }

  buf[used] = '\0';
  return used;
}

int u_getchar(void)
{
  unsigned char ch;
  for (;;) {
    int n = read(FD_STDIN, &ch, 1);
    if (n > 0) {
      return (int)ch;
    }
    if (n == 0) {
      // EOF（目前你大概用不到），用 -1 表示
      return -1;
    }
    // n < 0: 将来如果你有错误码，可以直接返回 n
    return n;
  }
}

/*
 * 从 stdin 读一行：
 *   - 一次 read 1 字节，一直读到 '\n' 或 '\r'（行尾不写入 buf）
 *   - buf 永远是以 '\0' 结尾的字符串，例如 "exit"、"hello"
 * 返回值：
 *   >0 : 实际写入的字符数（不含行尾，不含 '\0'）
 *   =0 : EOF（目前基本不会用到）
 *   <0 : 错误（将来有错误码可以透传）
 */
int u_gets(char *buf, int buf_size)
{
  return u_read_line(FD_STDIN, buf, buf_size);
}

int u_readn(int fd, void *buf, int nbytes)
{
  char *p   = (char *)buf;
  int total = 0;

  while (total < nbytes) {
    int n = read(fd, p + total, (uint64_t)(nbytes - total));
    if (n < 0) {
      // 将来有错误码可以直接 return n
      return n;
    }
    if (n == 0) {
      // EOF，提前结束
      break;
    }
    total += n;
  }
  return total;  // 可能 < nbytes（遇到 EOF）
}

int u_read_until(int fd, char *buf, int buf_size, char delim)
{
  if (buf_size <= 1) {
    return -1;
  }

  int used = 0;

  for (;;) {
    if (used >= buf_size - 1) {
      // 缓冲区满了
      break;
    }

    int n = read(fd, buf + used, 1);
    if (n < 0) {
      return n;
    }
    if (n == 0) {
      // EOF
      break;
    }

    char c = buf[used];
    used++;

    if (c == delim) {
      break;  // 读到了分隔符
    }
  }

  buf[used] = '\0';
  return used;
}
