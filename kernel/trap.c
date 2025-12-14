// kernel/trap.c

#include <stdint.h>
#include <time.h>
#include "riscv_csr.h"
#include "platform.h"
#include "trap.h"
#include "thread.h"
#include "usyscall.h"
#include "log.h"
#include "panic.h"
#include "sysfile.h"
#include "lock.h"
#include "cpu.h"
#include "sched.h"
#include "sched.h"

#ifndef NDEBUG
extern void print_thread_prefix(void);
#endif
extern void trap_entry(void);

static void dump_trap(struct trapframe *tf);
static void dump_backtrace_from_tf(const struct trapframe *tf, tid_t tid);

void trap_init(void) {
  reg_t addr = (reg_t)trap_entry;
  reg_t val  = addr & ~((reg_t)STVEC_MODE_MASK);
  val |= STVEC_MODE_DIRECT;
  csr_write(stvec, val);
}

static void breakpoint_handler(struct trapframe *tf) {
  const reg_t scause    = tf->scause;
  const uintptr_t sepc  = tf->sepc;
  const uintptr_t stval = tf->stval;
  const reg_t sstatus   = tf->sstatus;

  const int from_kernel =
      (sstatus & SSTATUS_SPP) != 0;  // 1 = S-mode, 0 = U-mode

#ifndef NDEBUG
  /* -------- Debug 版本：打印信息，尽量帮你定位 -------- */

  pr_debug("*** BREAKPOINT (ebreak) ***");
  pr_debug("  from=%s-mode sepc=0x%016lx", from_kernel ? "S" : "U",
           (unsigned long)sepc);
  pr_debug("  scause=0x%016lx stval=0x%016lx sstatus=0x%016lx",
           (unsigned long)scause, (unsigned long)stval, (unsigned long)sstatus);

  dump_backtrace_from_tf(tf, thread_current());

  if (!from_kernel) {
    /* 用户态 ebreak：类似 SIGTRAP
     * 策略：跳过 ebreak，然后把当前线程干掉，避免用户态死循环。
     */
    tf->sepc = sepc + 4;
    thread_sys_exit(tf, -1);  // 不一定会返回（内部可能 schedule）
    return;
  }

  /* 内核态 ebreak：大多数就是你写的 BREAK_IF()
   * Debug 下默认策略：打印一堆信息，然后跳过 ebreak 继续执行。
   */
  tf->sepc = sepc + 4;
  return;

#else /* NDEBUG */

  /* -------- Release 版本：出现在这里就是严重 bug -------- */
  dump_trap(tf);
  panic("EXC_BREAKPOINT in release build");
  // NOT REACHED

#endif /* NDEBUG */
}

static void syscall_handler(struct trapframe *tf) {
  const uintptr_t sys_id = tf->a0;

  uint64_t nwrite, nread;

  tf->sepc += 4;

  switch (sys_id) {
    case SYS_SLEEP:
      thread_sys_sleep(tf, tf->a1);
      // 里面可能 schedule()
      break;
    case SYS_THREAD_EXIT:
      thread_sys_exit(tf, (int)tf->a1);
      // 通常不会“回到这个线程”的 trap_return
      break;
    case SYS_THREAD_JOIN:
      thread_sys_join(tf, (tid_t)tf->a1, tf->a2);
      // 阻塞情况下，thread_sys_join 内部会调用 schedule()
      break;
    case SYS_THREAD_CREATE:
      thread_sys_create(tf, (thread_entry_t)tf->a1, (void *)tf->a2,
                        (const char *)tf->a3);
      break;
    case SYS_THREAD_KILL: {
      tid_t tid = (tid_t)tf->a1;
      thread_sys_kill(tf, tid);
      /* 返回值已经在 thread_sys_kill 里写入 tf->a0 了 */
      break;
    }
    case SYS_THREAD_LIST: {
      struct u_thread_info *ubuf = (struct u_thread_info *)tf->a1;
      int max                    = (int)tf->a2;
      int n                      = thread_sys_list(ubuf, max);
      tf->a0                     = (reg_t)n;
      break;
    }
    case SYS_WRITE:
      nwrite = sys_write((int)tf->a1, (const char *)tf->a2, (uint64_t)tf->a3);
      tf->a0 = nwrite;
      break;
    case SYS_READ: {
      int is_non_block_read = 0;
      nread = sys_read((int)tf->a1, (char *)tf->a2, (uint64_t)tf->a3, tf,
                       &is_non_block_read);
      if (is_non_block_read) {
        tf->a0 = nread;
      } else {
        /* 阻塞 read：
         * - sys_read 应该已经把当前线程挂到等待队列并 schedule()
         * - 被唤醒时，唤醒方会写回 t->tf.a0（你在 thread.c 里已经这么做了）
         */
      }
      break;
    }
    case SYS_CLOCK_GETTIME:
      tf->a0 = sys_clock_gettime((int)tf->a1, (struct timespec *)tf->a2);
      break;
    case SYS_IRQ_GET_STATS:
      tf->a0 = sys_irq_get_stats((struct irqstat_user *)tf->a1, (size_t)tf->a2);
      break;
    case SYS_GET_HARTID:
      tf->a0 = (reg_t)cpu_current_hartid();
      break;
    case SYS_YIELD:
      thread_sys_yield(tf);
      break;
    case SYS_THREAD_DETACH:
      thread_sys_detach(tf, (tid_t)tf->a1);
      break;
    default:
      dump_trap(tf);
      panic("unknown syscall");
      break;
  }
}

struct trapframe *trap_entry_c(struct trapframe *tf) {
  reg_t irq_state = kernel_lock();
  struct trapframe *ret;

  const reg_t scause      = tf->scause;
  const uintptr_t sstatus = tf->sstatus;
  const reg_t code        = scause_code(scause);

#ifndef NDEBUG
  // const unsigned long old_sepc   = tf->sepc;
  // const unsigned long old_s0     = tf->s0;
  // const unsigned long old_scause = tf->scause;

  // pr_debug("trap_entry_c: ENTER sepc=0x%lx s0=0x%lx scause=0x%lx", old_sepc,
  //          old_s0, old_scause);
  // print_thread_prefix();
#endif

  if (scause_is_interrupt(scause)) {
    switch (code) {
      case IRQ_SOFT_S:
        /* Software interrupt (IPI) */
        sched_on_ipi_irq(tf);
        goto handled;
      case IRQ_TIMER_S:
        /* S-mode timer interrupt */
        sched_on_timer_irq(tf);  // 内部可能调用 schedule()
        goto handled;
      case IRQ_EXT_S:
        platform_handle_s_external(tf);
        goto handled;
      default:
        break;  // 落到下面的“未处理 trap”
    }
  } else {
    switch (code) {
      case EXC_ENV_CALL_U:
        syscall_handler(tf);
        goto handled;
      case EXC_BREAKPOINT:
        breakpoint_handler(tf);
        goto handled;
      case EXC_ILLEGAL_INSTR:
        platform_puts("Illegal instruction\n");
        if ((sstatus & SSTATUS_SPP) == 0) {
          // 对用户态非法指令，终止当前线程（类似 SIGILL）
          thread_sys_exit(tf, -1);
          goto handled;
        }
        break;
        /* TODO: 其它异常 (page fault 等) 可以在这里继续加 case */
      default:
        break;
    }
  }

  dump_trap(tf);
  panic("unhandled trap");

handled:

#ifndef NDEBUG
  // pr_debug("trap_entry_c: LEAVE sepc=0x%lx s0=0x%lx", (unsigned
  // long)tf->sepc,
  //          (unsigned long)tf->s0);
#endif

  ret = cpu_this()->cur_tf;
  kernel_unlock(irq_state);
  return ret;
}

/* ---------- 调试用：打印完整 trap 信息 ---------- */

static void dump_backtrace_from_tf(const struct trapframe *tf, tid_t tid) {
  (void)tid; /* 如果暂时不用 tid，避免编译告警 */

  /* RISC-V GCC 在 -fno-omit-frame-pointer 下：
   *   prologue 典型为：
   *     addi sp, sp, -16
   *     sd   ra, 8(sp)
   *     sd   s0, 0(sp)
   *     addi s0, sp, 16
   *
   * 所以：
   *   当前帧的 frame pointer = s0
   *   前一帧的 s0 保存在 [s0 - 16]
   *   当前帧保存的 ra 在 [s0 -  8]
   */
  uintptr_t fp0 = tf->s0;

  if (fp0 == 0) {
    platform_puts("  backtrace: <no frame pointer>\n");
    return;
  }

  /* 以 trap 时的 sp 为中心限定一个窗口，避免乱跑栈：
   * 这里只是个保守估计，根据你实际的栈大小可以调大/调小。
   */
  const uintptr_t approx_sp      = tf->sp;
  const uintptr_t MAX_STACK_SCAN = 16 * 1024; /* 16 KiB */

  uintptr_t stack_lo             = approx_sp - MAX_STACK_SCAN;
  uintptr_t stack_hi             = approx_sp + MAX_STACK_SCAN;

  /* 处理一下 underflow 的情况 */
  if (stack_lo > approx_sp) {
    stack_lo = 0;
  }

  platform_puts("  backtrace:\n");

  /* frame #0：trap 发生时的 PC/RA */
  platform_puts("    #0  pc=");
  platform_put_hex64((uint64_t)tf->sepc);
  platform_puts("  ra=");
  platform_put_hex64((uint64_t)tf->ra);
  platform_puts("\n");

  uintptr_t fp        = fp0;
  const int MAX_DEPTH = 16; /* 防止死循环/栈太深 */

  for (int depth = 1; depth < MAX_DEPTH; ++depth) {
    /* 简单栈范围检查 */
    if (fp < stack_lo + 2 * sizeof(uintptr_t) || fp > stack_hi) {
      break;
    }

    uintptr_t *frame   = (uintptr_t *)fp;

    /* 布局：
     *   frame[-2] = prev s0 (上一帧的 fp)
     *   frame[-1] = saved ra
     */
    uintptr_t saved_ra = frame[-1];
    uintptr_t prev_fp  = frame[-2];

    if (saved_ra == 0 || prev_fp == 0) {
      break;
    }

    /* 栈向下增长，往上一帧走时 fp 应该单调递增 */
    if (prev_fp <= fp) {
      break;
    }

    platform_puts("    #");
    platform_put_dec_us((uint64_t)depth);
    platform_puts("  ra=");
    platform_put_hex64((uint64_t)saved_ra);
    platform_puts("\n");

    fp = prev_fp;
  }
}

static void dump_trap(struct trapframe *tf) {
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
    sys_id = tf->a0;
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

#ifndef NDEBUG
  dump_backtrace_from_tf(tf, tid);
#endif /* NDEBUG */
}
