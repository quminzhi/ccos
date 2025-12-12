#pragma once
#include <stdint.h>
#include "types.h"
#include "cpu_defs.h"

/*
 * TP 与 CPU 约定
 *
 * - 在内核中，tp 永远指向当前 CPU 的 struct cpu（per-CPU 结构）
 * - tp 是 per-CPU 的，不是 per-thread 的。
 */

#define NO_BOOT_HART 0xFFFFFFFF

extern volatile uint32_t g_boot_hartid;
extern volatile int smp_boot_done;

struct cpu {
  /* assembly fixed */
  uintptr_t kstack_top;      // [0]  本 hart trap 用内核栈顶
  struct trapframe *cur_tf;  // [8]  本 hart 当前线程的 trapframe*

  /* c related not fixed */
  uint32_t hartid;
  uint32_t online;
  tid_t current_tid;
  tid_t idle_tid;  // dle tid = hartid

  uint64_t timer_irqs;
  uint64_t ctx_switches;
};

typedef struct cpu cpu_t;

extern cpu_t g_cpus[MAX_HARTS];
extern uint8_t g_kstack[MAX_HARTS][KSTACK_SIZE];

static inline cpu_t *cpu_this(void) {
  cpu_t *c;
  __asm__ volatile("mv %0, tp" : "=r"(c));
  return c;
}

static inline uint32_t cpu_current_hartid(void) {
  return cpu_this()->hartid;
}

static inline uintptr_t cpu_kstack_top(uint32_t hartid) {
  return (uintptr_t)&g_kstack[hartid][KSTACK_SIZE];
}

static inline void smp_mb(void) {
  __asm__ volatile("fence rw,rw" ::: "memory");
}

void cpu_init_this_hart(uintptr_t hartid);
void cpu_enter_idle(uint32_t hartid);

void set_smp_boot_done();
void wait_for_smp_boot_done();
