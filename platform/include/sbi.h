// sbi.h
#pragma once
#include <stdint.h>

/*
 * 通用 SBI 调用封装（SBI v0.2+ 调用约定）
 * a7 = EID, a6 = FID, a0..a5 = 参数
 * 返回：a0 = error, a1 = value
 * 规范见 SBI 文档 Binary Encoding 部分。
 */

struct sbiret {
  long error;
  long value;
};

static inline struct sbiret sbi_call(long eid, long fid,
                                     long arg0, long arg1,
                                     long arg2, long arg3)
{
  register long a0 asm("a0") = arg0;
  register long a1 asm("a1") = arg1;
  register long a2 asm("a2") = arg2;
  register long a3 asm("a3") = arg3;
  register long a6 asm("a6") = fid;
  register long a7 asm("a7") = eid;

  asm volatile("ecall"
               : "+r"(a0), "+r"(a1)
               : "r"(a2), "r"(a3), "r"(a6), "r"(a7)
               : "memory");

  struct sbiret ret = { .error = a0, .value = a1 };
  return ret;
}

/* ===== Timer Extension (EID "TIME" = 0x54494D45, FID 0: set_timer) =====
 * Spec: Chapter "Timer Extension (EID #0x54494D45 \"TIME\")"
 */

#define SBI_EID_TIME          0x54494D45L
#define SBI_FID_SET_TIMER     0

static inline void sbi_set_timer(uint64_t stime_value)
{
  (void)sbi_call(SBI_EID_TIME, SBI_FID_SET_TIMER,
                 (long)stime_value, 0, 0, 0);
}

/* ===== Debug Console Extension (EID "DBCN" = 0x4442434E) =====
 * FID 0: write, FID 1: read, FID 2: write_byte
 */

#define SBI_EID_DBCN                      0x4442434EL
#define SBI_FID_DBCN_CONSOLE_WRITE       0
#define SBI_FID_DBCN_CONSOLE_READ        1
#define SBI_FID_DBCN_CONSOLE_WRITE_BYTE  2

static inline void sbi_debug_console_write_byte(uint8_t byte)
{
  (void)sbi_call(SBI_EID_DBCN, SBI_FID_DBCN_CONSOLE_WRITE_BYTE,
                 (long)byte, 0, 0, 0);
}

/* 简单的 puts（只依赖 DBCN，不做 legacy fallback） */
static inline void sbi_console_puts(const char *s)
{
  while (*s) {
    if (*s == '\n')
      sbi_debug_console_write_byte('\r');
    sbi_debug_console_write_byte((uint8_t)*s++);
  }
}
