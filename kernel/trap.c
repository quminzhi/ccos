// kernel/trap.c

#include <stdint.h>
#include "riscv_csr.h"
#include "platform.h"
#include "trap.h"
#include "thread.h"
#include "syscall.h"
#include "log.h"
#include "panic.h"

extern void trap_entry(void);

#ifndef KERNEL_STACK_SIZE
#define KERNEL_STACK_SIZE 4096
#endif
static uint8_t kernel_stack[KERNEL_STACK_SIZE];

static void dump_trap(struct trapframe *tf);

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

static void timer_handler(struct trapframe *tf)
{
  threads_tick();
  platform_timer_start_after(DELTA_TICKS);  // 大约 1s
  schedule(tf);
}

static uintptr_t syscall_handler(struct trapframe *tf)
{
  const uintptr_t sys_id = tf->a0;
  const uintptr_t sepc   = tf->sepc;

  switch (sys_id) {
    case SYS_SLEEP:
      ADVANCE_SEPC();
      thread_sys_sleep(tf, tf->a1);
      // 这里可能会 schedule，所以一定要在上面先改 sepc
      break;
    case SYS_THREAD_EXIT:
      ADVANCE_SEPC();
      thread_sys_exit(tf, (int)tf->a1);
      // 实际不会回到调用点
      break;
    case SYS_THREAD_JOIN:
      ADVANCE_SEPC();
      thread_sys_join(tf, (tid_t)tf->a1, tf->a2);
      // 如果 join 阻塞，schedule 会切到别的线程
      break;
    case SYS_THREAD_CREATE:
      ADVANCE_SEPC();
      thread_sys_create(tf, (thread_entry_t)tf->a1, (void *)tf->a2,
                        (const char *)tf->a3);
      break;

      // 以后想开 write/read，直接在这加 case 就行
      /*
      case SYS_WRITE:
        ADVANCE_SEPC();
        platform_sys_write(tf, (int)tf->a1, tf->a2, tf->a3);
        break;

      case SYS_READ:
        ADVANCE_SEPC();
        platform_sys_read(tf, (int)tf->a1, tf->a2, tf->a3);
        break;
      */

    default:
      ADVANCE_SEPC();  // 仍然要跳过 ecall 防止死循环
      dump_trap(tf);
      panic("unknown syscall");
      break;
  }

  return tf->sepc;
}

uintptr_t trap_entry_c(struct trapframe *tf)
{
  const reg_t scause      = tf->scause;
  const uintptr_t sepc    = tf->sepc;
  const uintptr_t sstatus = tf->sstatus;

  const int is_interrupt  = scause_is_interrupt(scause);
  const reg_t code        = scause_code(scause);

  /* ---------- 1. 先处理“预期内”的中断 ---------- */
  if (is_interrupt) {
    switch (code) {
      case IRQ_TIMER_S:
        /* S-mode timer interrupt */
        timer_handler(tf);
        return tf->sepc;

        // 以后有外部中断、软件中断，可以继续在这里加 case

      default:
        // 先跳到下面的“未处理 trap”打印逻辑
        break;
    }
  } else {
    /* ---------- 2. 再处理“预期内”的异常（syscall 等） ---------- */
    switch (code) {
      case EXC_ENV_CALL_U:
        syscall_handler(tf);
        return tf->sepc;

      case EXC_ILLEGAL_INSTR:
        // 这里先不马上死循环，分情况处理
        platform_puts("Illegal instruction\n");

#ifdef SSTATUS_SPP
        // SSTATUS_SPP == 0 表示从 U-mode trap 到 S-mode
        if ((sstatus & SSTATUS_SPP) == 0) {
          // 示例：对用户态非法指令，终止当前线程（类似 SIGILL）
          thread_sys_exit(tf, -1);
          return tf->sepc;  // thread_sys_exit 内部可能 schedule
        }
#endif
        // 否则是内核态非法指令，跌到下面的“未处理 trap”
        break;

      default:
        // 其它异常 (page fault 等) 可以在这里继续加 case
        break;
    }
  }

  dump_trap(tf);
  panic("unhandled trap");

  // NEVER GOES HERE
  if (!is_interrupt) {
    tf->sepc = sepc + 4;
  }
  return tf->sepc;
}

/* ---------- 调试用：打印完整 trap 信息 ---------- */
static void dump_trap(struct trapframe *tf)
{
  reg_t scause      = tf->scause;
  uintptr_t stval   = tf->stval;
  uintptr_t sepc    = tf->sepc;
  uintptr_t sstatus = tf->sstatus;

  int is_interrupt  = scause_is_interrupt(scause);
  reg_t code        = scause_code(scause);

  /* 当前线程信息 */
  tid_t tid         = thread_current();
  const char *name  = thread_name(tid);

  /* 根据 sstatus.SPP 判断上一层是 U 还是 S：0=U, 1=S */
  char mode_char    = (sstatus & SSTATUS_SPP) ? 'S' : 'U';

  /* 如果是来自 U-mode 的 ECALL，则把 syscall 号打印出来 */
  int is_syscall    = (!is_interrupt && code == EXC_ENV_CALL_U);
  uintptr_t sys_id  = 0;
  if (is_syscall) {
    sys_id = tf->a0; /* 你在 syscall_handler 里就是用 a0 当 syscall id 的
                        :contentReference[oaicite:3]{index=3} */
  }

  platform_puts("\n*** TRAP ***\n");

  /* 线程 + 模式 */
  platform_puts("  thread=[");
  platform_put_hex64((uintptr_t)tid);
  platform_putc(':');
  platform_puts(name);
  platform_putc(':');
  platform_putc(mode_char); /* 'U' or 'S' */
  platform_puts("]\n");

  /* 类型 + code */
  platform_puts("  kind=");
  if (is_interrupt) {
    platform_puts("interrupt");
  } else {
    platform_puts("exception");
  }
  platform_puts(" code=");
  platform_put_hex64(code);

  if (!is_interrupt) {
    if (code == EXC_ILLEGAL_INSTR) {
      platform_puts(" (Illegal instruction)");
    } else if (code == EXC_ENV_CALL_M) {
      platform_puts(" (ECALL from M-mode)");
    } else if (code == EXC_ENV_CALL_S) {
      platform_puts(" (ECALL from S-mode)");
    } else if (code == EXC_ENV_CALL_U) {
      platform_puts(" (ECALL from U-mode)");
    }
  }
  platform_puts("\n");

  /* sepc + syscall 号 */
  platform_puts("  sepc=");
  platform_put_hex64(sepc);
  if (is_syscall) {
    platform_puts("  syscall_id=");
    platform_put_hex64(sys_id);
  }
  platform_puts("\n");

  /* 其他 csr */
  platform_puts("  scause=");
  platform_put_hex64(scause);
  platform_puts(" stval=");
  platform_put_hex64(stval);
  platform_puts(" sstatus=");
  platform_put_hex64(sstatus);
  platform_puts("\n");
}
