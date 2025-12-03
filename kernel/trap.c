// kernel/trap.c

#include <stdint.h>
#include "riscv_csr.h"
#include "platform.h"
#include "trap.h"
#include "thread.h"
#include "syscall.h"
#include "log.h"

extern void trap_entry(void);

#ifndef KERNEL_STACK_SIZE
#define KERNEL_STACK_SIZE 4096
#endif
static uint8_t kernel_stack[KERNEL_STACK_SIZE];

void trap_init(void)
{
  /* 1. 设置 stvec = trap_entry（direct 模式） */
  reg_t stvec = (reg_t)trap_entry;
  stvec &= ~((reg_t)STVEC_MODE_MASK);
  stvec |= STVEC_MODE_DIRECT;
  csr_write(stvec, stvec);

  /* 2. 设置 sscratch = 内核栈顶 */
  reg_t ksp = (reg_t)(kernel_stack + KERNEL_STACK_SIZE);
  ksp &= ~(reg_t)0xFUL;
  csr_write(sscratch, ksp);

  // 这里先关掉所有 S-mode 中断，由后面的 enable_xxx 再打开
  csr_clear(sstatus, SSTATUS_SIE);
  csr_clear(sie, SIE_STIE | SIE_SEIE | SIE_SSIE);

  // /* 4. 为 platform0 配一个非 0 的优先级，并在当前 hart 使能它 */
  // plic_set_priority(PLIC_IRQ_platform0, 1);
  // plic_enable_irq(PLIC_IRQ_platform0);
}

/*
 * trap_entry_c 由 arch/riscv/trap.S 调用：
 *
 * trap_entry:
 *   保存最小现场 (ra 等)
 *   call trap_entry_c
 *   恢复现场
 *   sret
 */
uintptr_t trap_entry_c(struct trapframe *tf)
{
  reg_t scause      = tf->scause;
  uintptr_t stval   = tf->stval;
  uintptr_t sepc    = tf->sepc;
  uintptr_t sstatus = tf->sstatus;

  int is_interrupt  = scause_is_interrupt(scause);
  reg_t code        = scause_code(scause);

  /* ---------- 机器定时器中断：tick + 更新睡眠 + 调度 ---------- */
  if (is_interrupt && code == IRQ_TIMER_S) {
    /* S-mode timer interrupt */
    threads_tick();
    platform_timer_start_after(10000000UL);  // 大约 1s
    schedule(tf);
    return tf->sepc;
  }

  /* ---------- ECALL from S-mode：作为简单 syscall 入口 ---------- */
  if (!is_interrupt && code == EXC_ENV_CALL_U) {
    uintptr_t sys_id = tf->a0;

    if (sys_id == SYS_SLEEP) {
      /* sleep(ticks) 的“内核实现”，放在线程模块里 */
      tf->sepc = sepc + 4;
      thread_sys_sleep(tf, tf->a1);
      // WARNING: tf->sepc = sepc + 4; 此时可能是下一个线程的mepc
      // 凡是你想写回“当前线程”的 tf 字段（比如 mepc），一定要在调用 schedule
      // 前改。
      return tf->sepc;
    } else if (sys_id == SYS_THREAD_EXIT) {
      tf->sepc = sepc + 4;
      thread_sys_exit(tf, (int)tf->a1);
      return tf->sepc;  // 实际不会回到调用 thread_exit 的那一行
    } else if (sys_id == SYS_THREAD_JOIN) {
      tf->sepc = sepc + 4;
      thread_sys_join(tf, (tid_t)tf->a1, tf->a2);
      return tf->sepc;  // 如果 join 阻塞，schedule 会切到别的线程
    } else if (sys_id == SYS_THREAD_CREATE) {
      tf->sepc = sepc + 4;
      thread_sys_create(tf, (thread_entry_t)tf->a1, (void *)tf->a2,
                        (const char *)tf->a3);
      return tf->sepc;
      // } else if (sys_id == SYS_WRITE) {
      //   tf->sepc = sepc + 4;
      //   platform_sys_write(tf, (int)tf->a1, tf->a2, tf->a3);
      //   return tf->sepc;
      // } else if (sys_id == SYS_READ) {
      //   tf->sepc = sepc + 4;
      //   platform_sys_read(tf, (int)tf->a1, tf->a2, tf->a3);
      //   return tf->sepc;
    } else {
      platform_puts("Unknown syscall id=");
      platform_put_hex64(sys_id);
      platform_puts("\n");
      /* sys 未处理，照样跳过 ecall 防止死循环 */
      tf->sepc = sepc + 4;
      return tf->sepc;
    }
  }

  /* ---------- 其他中断 / 异常：先简单打印一波 ---------- */

  platform_puts(">> trap: ");
  if (is_interrupt) {
    platform_puts("interrupt");
  } else {
    platform_puts("exception");
  }
  platform_puts(" code=");
  platform_put_hex64(code);

  if (!is_interrupt) {
    /* 常见异常的 decode：用 EXC_* 枚举，避免魔法数字 */
    if (code == EXC_ILLEGAL_INSTR) {
      platform_puts(" (Illegal instruction)");
    } else if (code == EXC_ENV_CALL_M) {
      platform_puts(" (ECALL from M-mode)");
    } else if (code == EXC_ENV_CALL_S) {
      platform_puts(" (ECALL from S-mode)");
    } else if (code == EXC_ENV_CALL_U) {
      platform_puts(" (ECALL from U-mode)");
    }
    /* 需要的话可以继续补充更多异常 decode */
  } else {
    /* 中断的话，这里可以根据 IRQ_* 做一些通用 decode（暂时略） */
  }

  platform_puts(", scause=");
  platform_put_hex64(scause);
  platform_puts(", sepc=");
  platform_put_hex64(sepc);
  platform_puts(", stval=");
  platform_put_hex64(stval);
  platform_puts(", sstatus=");
  platform_put_hex64(sstatus);
  platform_puts("\n");

  for (;;) {}

  /* 对“异常”（同步 trap）默认策略：跳过当前指令 */
  if (!is_interrupt) {
    tf->sepc = sepc + 4;
  }
  /* 对“中断”（异步 trap），保持 mepc 不变，按 RISC-V 语义继续执行 */

  return tf->sepc;
}
