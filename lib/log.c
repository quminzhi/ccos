#include "log.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ============================================================
 *  INTERNAL STATE
 * ============================================================ */

static log_write_fn_t s_write_fn = 0;
static volatile log_level_t s_log_level = LOG_RUNTIME_DEFAULT_LEVEL;
static volatile log_path_mode_t s_path_mode = LOG_DEFAULT_PATH_MODE;

#if LOG_ENABLE_TIMESTAMP
static log_timestamp_fn_t s_ts_fn = 0;
#endif

#if LOG_USE_RING_BUFFER
static char s_ring_buf[LOG_RING_BUFFER_SIZE];
static size_t s_ring_head = 0; /* next write position */
static size_t s_ring_size = 0; /* used bytes */
#endif

/* ============================================================
 *  SMALL INTERNAL HELPERS
 * ============================================================ */

static void
log_append_char(char *buf, int *pos, int max, char c) {
  if (*pos < max - 1) {
    buf[(*pos)++] = c;
  }
}

static void
log_append_str(char *buf, int *pos, int max, const char *s) {
  if (!s) {
    s = "(null)";
  }
  while (*s && *pos < max - 1) {
    buf[(*pos)++] = *s++;
  }
}

static void
log_append_uint(char *buf,
                int *pos,
                int max,
                unsigned long long v,
                unsigned base,
                bool upper,
                int width,
                char pad) {
  char tmp[32];
  int idx = 0;

  if (v == 0) {
    tmp[idx++] = '0';
  } else {
    while (v != 0 && idx < (int) sizeof(tmp)) {
      unsigned digit = (unsigned) (v % base);
      v /= base;

      char ch;
      if (digit < 10)
        ch = (char) ('0' + digit);
      else
        ch = (char) ((upper ? 'A' : 'a') + (digit - 10));
      tmp[idx++] = ch;
    }
  }

  int num_digits = idx;
  if (width < num_digits)
    width = num_digits;

  int pad_count = width - num_digits;
  while (pad_count-- > 0 && *pos < max - 1) {
    buf[(*pos)++] = pad;
  }

  while (idx > 0 && *pos < max - 1) {
    buf[(*pos)++] = tmp[--idx];
  }
}

/* Very small printf-like formatter.
 * Supported:
 *   %s, %c, %d/%i, %u, %x/%X, %p, %%
 * Width & '0' pad supported, ll/ld/lu 也支持。
 */
static int
log_vformat(char *dst, int dst_size, const char *fmt, va_list ap) {
  int pos = 0;

  for (const char *p = fmt; *p && pos < dst_size - 1; ++p) {
    if (*p != '%') {
      log_append_char(dst, &pos, dst_size, *p);
      continue;
    }

    ++p; /* skip '%' */
    if (*p == '%') {
      log_append_char(dst, &pos, dst_size, '%');
      continue;
    }

    char pad = ' ';
    int width = 0;

    if (*p == '0') {
      pad = '0';
      ++p;
    }

    while (*p >= '0' && *p <= '9') {
      width = width * 10 + (*p - '0');
      ++p;
    }

    int len_mod = 0; /* 0: int, 1: long, 2: long long */
    if (*p == 'l') {
      ++p;
      len_mod = 1;
      if (*p == 'l') {
        ++p;
        len_mod = 2;
      }
    }

    char spec = *p;

    switch (spec) {
      case 's': {
        const char *s = va_arg(ap, const char *);
        log_append_str(dst, &pos, dst_size, s);
        break;
      }
      case 'c': {
        int c = va_arg(ap, int);
        log_append_char(dst, &pos, dst_size, (char) c);
        break;
      }
      case 'd':
      case 'i': {
        long long v;
        if (len_mod == 2) { /* %lld */
          v = va_arg(ap, long long);
        } else if (len_mod == 1) { /* %ld */
          v = va_arg(ap, long);
        } else { /* %d */
          v = va_arg(ap, int);
        }

        unsigned long long u;
        if (v < 0) {
          log_append_char(dst, &pos, dst_size, '-');
          u = (unsigned long long) (-v);
          if (width > 0)
            width--; /* one char used by '-' */
        } else {
          u = (unsigned long long) v;
        }
        log_append_uint(dst, &pos, dst_size, u, 10, false, width, pad);
        break;
      }
      case 'u': {
        unsigned long long v;
        if (len_mod == 2) { /* %llu */
          v = va_arg(ap, unsigned long long);
        } else if (len_mod == 1) { /* %lu */
          v = va_arg(ap, unsigned long);
        } else { /* %u */
          v = va_arg(ap, unsigned int);
        }
        log_append_uint(dst, &pos, dst_size, (unsigned long long) v, 10, false, width, pad);
        break;
      }
      case 'x':
      case 'X': {
        unsigned long long v;
        if (len_mod == 2) {
          v = va_arg(ap, unsigned long long);
        } else if (len_mod == 1) {
          v = va_arg(ap, unsigned long);
        } else {
          v = va_arg(ap, unsigned int);
        }
        log_append_uint(dst, &pos, dst_size, (unsigned long long) v, 16, (spec == 'X'), width, pad);
        break;
      }
      case 'p': {
        void *ptr = va_arg(ap, void *);
        log_append_str(dst, &pos, dst_size, "0x");
        uintptr_t v = (uintptr_t) ptr;
        log_append_uint(dst, &pos, dst_size, (unsigned long long) v, 16, false, 0, ' ');
        break;
      }
      default:
        /* Unknown specifier: print literally. */
        log_append_char(dst, &pos, dst_size, '%');
        log_append_char(dst, &pos, dst_size, spec);
        break;
    }
  }

  dst[pos] = '\0';
  return pos;
}

/* Tiny local printable check (no libc isprint dependency). */
static int
log_isprint(unsigned char c) {
  return (c >= 0x20u && c <= 0x7Eu);
}

/* ============================================================
 *  PUBLIC API
 * ============================================================ */

void
log_init(log_write_fn_t fn) {
  s_write_fn = fn;
}

void
log_set_level(log_level_t level) {
  s_log_level = level;
}

log_level_t
log_get_level(void) {
  return s_log_level;
}

void
log_set_path_mode(log_path_mode_t mode) {
  s_path_mode = mode;
}

log_path_mode_t
log_get_path_mode(void) {
  return s_path_mode;
}

const char *
log_level_to_string(log_level_t level) {
  switch (level) {
    case LOG_LEVEL_ERROR:
      return "E";
    case LOG_LEVEL_WARN:
      return "W";
    case LOG_LEVEL_INFO:
      return "I";
    case LOG_LEVEL_DEBUG:
      return "D";
    case LOG_LEVEL_TRACE:
      return "T";
    default:
      return "?";
  }
}

const char *
log_level_to_full_string(log_level_t level) {
  switch (level) {
    case LOG_LEVEL_OFF:
      return "OFF";
    case LOG_LEVEL_ERROR:
      return "ERROR";
    case LOG_LEVEL_WARN:
      return "WARN";
    case LOG_LEVEL_INFO:
      return "INFO";
    case LOG_LEVEL_DEBUG:
      return "DEBUG";
    case LOG_LEVEL_TRACE:
      return "TRACE";
    default:
      return "UNKNOWN";
  }
}

#if LOG_ENABLE_TIMESTAMP
void
log_set_timestamp_fn(log_timestamp_fn_t fn) {
  s_ts_fn = fn;
}
#endif

#if LOG_USE_RING_BUFFER
static void
ring_write(const char *data, size_t len) {
  if (len == 0)
    return;

  if (len > LOG_RING_BUFFER_SIZE) {
    data += (len - LOG_RING_BUFFER_SIZE);
    len = LOG_RING_BUFFER_SIZE;
  }

  for (size_t i = 0; i < len; ++i) {
    size_t idx = (s_ring_head + i) % LOG_RING_BUFFER_SIZE;
    s_ring_buf[idx] = data[i];
  }

  s_ring_head = (s_ring_head + len) % LOG_RING_BUFFER_SIZE;

  if (s_ring_size + len >= LOG_RING_BUFFER_SIZE)
    s_ring_size = LOG_RING_BUFFER_SIZE;
  else
    s_ring_size += len;
}
#endif /* LOG_USE_RING_BUFFER */

int
log_vprintf(
    log_level_t level, const char *file, int line, const char *func, const char *fmt, va_list ap) {
  char buf[LOG_BUFFER_SIZE];
  int pos = 0;

  if (level <= LOG_LEVEL_OFF || level > s_log_level) {
    return 0;
  }

#if !LOG_USE_RING_BUFFER
  if (s_write_fn == 0) {
    return 0;
  }
#endif

  /* Optional timestamp: "[1234] " */
#if LOG_ENABLE_TIMESTAMP
  if (s_ts_fn) {
    uint32_t ts = s_ts_fn();
    log_append_char(buf, &pos, LOG_BUFFER_SIZE, '[');
    log_append_uint(buf, &pos, LOG_BUFFER_SIZE, (unsigned long long) ts, 10, false, 0, ' ');
    log_append_char(buf, &pos, LOG_BUFFER_SIZE, ']');
    log_append_char(buf, &pos, LOG_BUFFER_SIZE, ' ');
  }
#endif

  /* Level + location prefix: "[I] file:line func(): " */
  log_append_char(buf, &pos, LOG_BUFFER_SIZE, '[');
  const char *level_str = log_level_to_string(level);
  log_append_str(buf, &pos, LOG_BUFFER_SIZE, level_str);
  log_append_char(buf, &pos, LOG_BUFFER_SIZE, ']');
  log_append_char(buf, &pos, LOG_BUFFER_SIZE, ' ');

  if (file && s_path_mode != LOG_PATH_NONE) {
    const char *file_to_print = file;

    if (s_path_mode == LOG_PATH_BASENAME) {
      /* Strip directory prefixes: keep only chars after last '/' or '\\'. */
      const char *base = file;
      for (const char *p = file; *p; ++p) {
        if (*p == '/' || *p == '\\') {
          base = p + 1;
        }
      }
      file_to_print = base;
    }

    log_append_str(buf, &pos, LOG_BUFFER_SIZE, file_to_print);
    log_append_char(buf, &pos, LOG_BUFFER_SIZE, ':');
    log_append_uint(buf, &pos, LOG_BUFFER_SIZE, (unsigned long long) line, 10, false, 0, ' ');
    log_append_char(buf, &pos, LOG_BUFFER_SIZE, ' ');
  }

  if (func) {
    log_append_str(buf, &pos, LOG_BUFFER_SIZE, func);
    log_append_str(buf, &pos, LOG_BUFFER_SIZE, "(): ");
  }

  /* Message body */
  pos += log_vformat(buf + pos, LOG_BUFFER_SIZE - pos, fmt, ap);

  if (pos < LOG_BUFFER_SIZE - 1) {
    buf[pos++] = '\n';
  }
  buf[pos] = '\0';

  LOG_LOCK();

#if LOG_USE_RING_BUFFER
  ring_write(buf, (size_t) pos);
#endif

  if (s_write_fn) {
    s_write_fn(buf, (size_t) pos);
  }

  LOG_UNLOCK();

  return pos;
}

int
log_printf(log_level_t level, const char *file, int line, const char *func, const char *fmt, ...) {
  int ret;
  va_list ap;
  va_start(ap, fmt);
  ret = log_vprintf(level, file, line, func, fmt, ap);
  va_end(ap);
  return ret;
}

/* ============================================================
 *  RING BUFFER API
 * ============================================================ */

#if LOG_USE_RING_BUFFER

size_t
log_ring_size(void) {
  return s_ring_size;
}

size_t
log_ring_capacity(void) {
  return LOG_RING_BUFFER_SIZE;
}

void
log_ring_clear(void) {
  LOG_LOCK();
  s_ring_size = 0;
  LOG_UNLOCK();
}

static size_t
ring_peek_internal(char *out, size_t maxlen) {
  if (s_ring_size == 0 || maxlen == 0)
    return 0;

  size_t to_read = (s_ring_size < maxlen) ? s_ring_size : maxlen;
  size_t tail = (s_ring_head + LOG_RING_BUFFER_SIZE - s_ring_size) % LOG_RING_BUFFER_SIZE;

  for (size_t i = 0; i < to_read; ++i) {
    size_t idx = (tail + i) % LOG_RING_BUFFER_SIZE;
    out[i] = s_ring_buf[idx];
  }

  return to_read;
}

size_t
log_ring_peek(char *out, size_t maxlen) {
  size_t n;
  LOG_LOCK();
  n = ring_peek_internal(out, maxlen);
  LOG_UNLOCK();
  return n;
}

size_t
log_ring_read(char *out, size_t maxlen) {
  size_t n;
  LOG_LOCK();
  n = ring_peek_internal(out, maxlen);
  if (n > 0) {
    s_ring_size -= n;
  }
  LOG_UNLOCK();
  return n;
}

#endif /* LOG_USE_RING_BUFFER */

/* ============================================================
 *  HEXDUMP
 * ============================================================ */

void
log_hexdump(log_level_t level,
            const char *file,
            int line,
            const char *func,
            const void *data,
            size_t len,
            const char *prefix) {
  if (level <= LOG_LEVEL_OFF || level > s_log_level)
    return;

  const uint8_t *p = (const uint8_t *) data;
  size_t offset = 0;

  char hexbuf[3 * 16 + 1];
  char asciibuf[16 + 1];

  while (offset < len) {
    size_t chunk = len - offset;
    if (chunk > 16)
      chunk = 16;

    int hpos = 0;
    for (size_t i = 0; i < chunk; ++i) {
      uint8_t b = p[offset + i];
      static const char hex_digits[] = "0123456789ABCDEF";
      if (hpos + 3 < (int) sizeof(hexbuf)) {
        hexbuf[hpos++] = hex_digits[(b >> 4) & 0xF];
        hexbuf[hpos++] = hex_digits[b & 0xF];
        hexbuf[hpos++] = ' ';
      }
      asciibuf[i] = log_isprint(b) ? (char) b : '.';
    }
    hexbuf[hpos] = '\0';
    asciibuf[chunk] = '\0';

    /* Note: no width on %X because our formatter doesn't handle %04X. */
    log_printf(level,
               file,
               line,
               func,
               "%s%X: %s |%s|",
               (prefix ? prefix : ""),
               (unsigned) offset,
               hexbuf,
               asciibuf);

    offset += chunk;
  }
}

/* ============================================================
 *  PANIC
 * ============================================================ */

/* 这里的 PANIC 直接复用 log_vprintf，打印一条 [E] ... PANIC(): msg，
 * 然后在 RISC-V 上 ebreak 并死循环。
 */
void
log_panicf_internal(const char *file, int line, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  /* func 填 "PANIC"，方便看前缀 */
  log_vprintf(LOG_LEVEL_ERROR, file, line, "PANIC", fmt, ap);
  va_end(ap);

#ifndef NDEBUG
  /* Debug 版：触发 ebreak，方便调试器直接停在 panic 现场 */
#ifdef __riscv
  __asm__ volatile("ebreak");
#endif
#endif

  /* Debug/Release 都会挂死在这里（你可以改成 reset 等） */
  for (;;) {
    /* 可选：低功耗一点可以 wfi */
    /* __asm__ volatile("wfi"); */
  }
}
