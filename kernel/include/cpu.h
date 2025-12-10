#pragma once
#include <stdint.h>
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
  uint32_t hartid;  // 这个 CPU 对应的 hartid
  uint32_t online;  // 是否已经上线 (0/1)，以后可以做更复杂状态机
                    // 预留一些以后用的字段：
                    // struct thread *current;
                    // uint64_t ticks;
};

typedef struct cpu cpu_t;

extern cpu_t g_cpus[MAX_HARTS];
extern uint8_t g_kstack[MAX_HARTS][KSTACK_SIZE];

static inline cpu_t *cpu_this(void)
{
  cpu_t *c;
  __asm__ volatile("mv %0, tp" : "=r"(c));
  return c;
}

static inline uint32_t cpu_current_hartid(void) { return cpu_this()->hartid; }

static inline uintptr_t cpu_kstack_top(uint32_t hartid)
{
  // 返回这个 hart 的“内核栈顶”地址
  return (uintptr_t)&g_kstack[hartid][KSTACK_SIZE];
}

static inline void smp_mb(void)
{
  __asm__ volatile("fence rw,rw" ::: "memory");
}

void cpu_init_this_hart(uintptr_t hartid);
void set_smp_boot_done();
void wait_for_smp_boot_done();
