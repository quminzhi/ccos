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

/* 内部：输出一个字符到 buf（带截断计算） */
static inline void out_char(char *buf, size_t *pos, size_t *total, size_t avail,
                            char ch)
{
  if (*pos < avail) {
    buf[*pos] = ch;
  }
  (*pos)++;
  (*total)++;
}

static int u_vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
  size_t total = 0;  // 理论输出长度（不含 '\0'）
  size_t pos   = 0;  // 实际写入位置
  size_t avail = 0;  // 最多可写字符数（预留 1 字节给 '\0'）

  if (size > 0) {
    avail = size - 1;
  }

  for (; *fmt; ++fmt) {
    if (*fmt != '%') {
      out_char(buf, &pos, &total, avail, *fmt);
      continue;
    }

    /* 解析 %[flags][width][.precision][length]specifier */
    ++fmt;
    if (*fmt == '\0') break;

    if (*fmt == '%') {
      out_char(buf, &pos, &total, avail, '%');
      continue;
    }

    /* -------- flags -------- */
    int left_adjust = 0;  // '-'
    int zero_pad    = 0;  // '0'

    for (;;) {
      if (*fmt == '-') {
        left_adjust = 1;
        ++fmt;
      } else if (*fmt == '0') {
        zero_pad = 1;
        ++fmt;
      } else {
        break;
      }
    }

    /* -------- width -------- */
    int width = 0;
    while (*fmt >= '0' && *fmt <= '9') {
      width = width * 10 + (*fmt - '0');
      ++fmt;
    }

    /* -------- precision (忽略，只是跳过) -------- */
    if (*fmt == '.') {
      ++fmt;
      while (*fmt >= '0' && *fmt <= '9') {
        ++fmt;
      }
    }

    /* -------- length 修饰符：l / ll / z -------- */
    enum { LEN_NONE = 0, LEN_L, LEN_LL, LEN_Z } length_mod = LEN_NONE;

    if (*fmt == 'l') {
      ++fmt;
      if (*fmt == 'l') {
        length_mod = LEN_LL;
        ++fmt;
      } else {
        length_mod = LEN_L;
      }
    } else if (*fmt == 'z') {
      length_mod = LEN_Z;
      ++fmt;
    }

    if (*fmt == '\0') break;

    char spec = *fmt;

    /* -------- 把值格式化到 tmp[] -------- */
    char tmp[64];
    char *str  = tmp;
    size_t len = 0;

    switch (spec) {
      case 'c': {
        int c  = va_arg(ap, int);
        tmp[0] = (char)c;
        str    = tmp;
        len    = 1;
        break;
      }

      case 's': {
        const char *s = va_arg(ap, const char *);
        if (!s) s = "(null)";
        str      = (char *)s;
        len      = u_strlen(s);
        zero_pad = 0;  // 字符串不做 0 填充
        break;
      }

      case 'd':
      case 'i': {
        long long v = 0;
        switch (length_mod) {
          case LEN_NONE:
            v = (long long)va_arg(ap, int);
            break;
          case LEN_L:
            v = (long long)va_arg(ap, long);
            break;
          case LEN_LL:
            v = (long long)va_arg(ap, long long);
            break;
          case LEN_Z:
            v = (long long)va_arg(ap, long);
            break;  // 简单处理
        }
        char *start = u_itoa(v, tmp + sizeof(tmp), 10, 1);
        str         = start;
        len         = (size_t)((tmp + sizeof(tmp)) - start);
        break;
      }

      case 'u':
      case 'x':
      case 'X': {
        unsigned long long v = 0;
        switch (length_mod) {
          case LEN_NONE:
            v = (unsigned long long)va_arg(ap, unsigned int);
            break;
          case LEN_L:
            v = (unsigned long long)va_arg(ap, unsigned long);
            break;
          case LEN_LL:
            v = (unsigned long long)va_arg(ap, unsigned long long);
            break;
          case LEN_Z:
            v = (unsigned long long)va_arg(ap, size_t);
            break;
        }
        int base    = (spec == 'u') ? 10 : 16;
        char *start = u_itoa((long long)v, tmp + sizeof(tmp), base, 0);
        str         = start;
        len         = (size_t)((tmp + sizeof(tmp)) - start);

        if (spec == 'X') {
          for (size_t i = 0; i < len; ++i) {
            if (str[i] >= 'a' && str[i] <= 'f') {
              str[i] -= ('a' - 'A');
            }
          }
        }
        break;
      }

      case 'p': {
        uintptr_t v = (uintptr_t)va_arg(ap, void *);
        char *start = u_itoa((long long)v, tmp + sizeof(tmp), 16, 0);
        *--start    = 'x';
        *--start    = '0';
        str         = start;
        len         = (size_t)((tmp + sizeof(tmp)) - start);
        zero_pad    = 0;
        break;
      }

      default: {
        /* 不支持的格式，输出 "%X" */
        out_char(buf, &pos, &total, avail, '%');
        out_char(buf, &pos, &total, avail, spec);
        continue;
      }
    }

    /* -------- 应用宽度/对齐/填充 -------- */
    size_t field_len = len;
    size_t pad_len   = 0;
    char pad_char    = ' ';

    if (width > 0 && (size_t)width > field_len) {
      pad_len = (size_t)width - field_len;
    }

    if (zero_pad && !left_adjust && spec != 's') {
      pad_char = '0';
    }

    /* 右对齐：pad 在前 */
    if (!left_adjust) {
      for (size_t i = 0; i < pad_len; ++i) {
        out_char(buf, &pos, &total, avail, pad_char);
      }
    }

    /* 输出实际内容 */
    for (size_t i = 0; i < len; ++i) {
      out_char(buf, &pos, &total, avail, str[i]);
    }

    /* 左对齐：pad 在后 */
    if (left_adjust) {
      for (size_t i = 0; i < pad_len; ++i) {
        out_char(buf, &pos, &total, avail, ' ');
      }
    }
  }

  /* 结尾 '\0' */
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

int u_gets(char *buf, int buf_size)
{
  if (!buf || buf_size <= 1) {
    return -1;
  }

  int used = 0;

  for (;;) {
    char c;
    int n = read(FD_STDIN, &c, 1);
    if (n < 0) {
      // 真正的 read 错误，直接返回错误码
      return n;
    }
    if (n == 0) {
      // EOF：如果一行一个字节都没读到，就返回 0
      if (used == 0) {
        return 0;
      }
      break;
    }

    unsigned char uc = (unsigned char)c;

    /* ---- Ctrl-C：中断当前行 ---- */
    if (uc == 0x03) {  // ASCII ETX，Ctrl-C
      // 回显 ^C 并换行（看起来和常见 shell 一样）
      u_putchar('^');
      u_putchar('C');
      u_putchar('\n');

      // 丢弃当前行
      buf[0] = '\0';
      used   = 0;

      // 告诉上层：这一行被 Ctrl-C 打断了
      return U_GETS_INTR;
    }

    /* ---- 行结束：回车/换行 ---- */
    if (c == '\n' || c == '\r') {
      u_putchar('\n');
      break;
    }

    /* ---- 退格：\b 或 DEL ---- */
    if (c == '\b' || uc == 0x7f) {
      if (used > 0) {
        used--;
        // 在终端上擦掉最后一个字符：左移、写空格、再左移
        u_putchar('\b');
        u_putchar(' ');
        u_putchar('\b');
      }
      continue;
    }

    /* 其它控制字符先忽略（你以后可以扩展） */
    if (uc < 0x20) {
      continue;
    }

    /* ---- 正常可见字符 ---- */
    if (used < buf_size - 1) {
      buf[used++] = c;
      u_putchar(c);
    } else {
      // 行太长：简单丢弃后续字符，也可以选择响铃一下
      // u_putchar('\a');
    }
  }

  buf[used] = '\0';
  return used;
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
