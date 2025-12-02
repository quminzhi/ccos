// kernel/trap.c

#include <stdint.h>
#include "riscv_csr.h"
#include "platform.h"
#include "log.h"
#include "trap.h"

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

  // /* 4. 为 UART0 配一个非 0 的优先级，并在当前 hart 使能它 */
  // plic_set_priority(PLIC_IRQ_UART0, 1);
  // plic_enable_irq(PLIC_IRQ_UART0);
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
  reg_t scause = csr_read(scause);
  reg_t stval  = csr_read(stval);
  reg_t code   = mcause_code(scause);

  if (mcause_is_interrupt(scause)) {
    /* S-mode timer interrupt */
    if (code == IRQ_TIMER_S) {
      kernel_timer_tick();
      return tf->sepc + 4;
    }

    /* 其他中断暂时没处理，用 pr_err 打印出来 */
    pr_err("unhandled interrupt: code=%llu scause=0x%llx",
           (unsigned long long)code, (unsigned long long)scause);
  } else {
    /* 异常：打印 scause/stval，方便调试 */
    pr_err("exception: code=%llu scause=0x%llx stval=0x%llx",
           (unsigned long long)code, (unsigned long long)scause,
           (unsigned long long)stval);
  }

  /* 当前阶段：遇到未处理 trap 直接停机 */
  for (;;) {
    __asm__ volatile("wfi");
  }
}

/* 每次 timer 中断打印 tick，并安排下一次定时器 */
void kernel_timer_tick(void)
{
  pr_info("timer tick");
  platform_timer_start_after(10000000UL);  // 大约 1s
}
