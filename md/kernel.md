# Kernel 概览（RISC-V S-mode 内核）

> 目标：**在 arch/** + platform/** 提供的抽象之上，实现最小但完整的「线程 + 中断 + syscall」内核。**

---

## 1. 内核层次结构

当前内核主要由这些部分组成：

* **`kernel/main.c`**

  * 内核入口 `kernel_main()`，负责系统初始化和启动第一个用户线程。
* **`kernel/trap.c`**

  * 统一的 trap 入口（异常 + 中断），分类、分发、panic。
* **`kernel/thread.c`**（或类似）

  * 线程对象、调度器、上下文切换。
* **`kernel/syscall.c`**

  * syscall 分发，内核服务入口。
* **`kernel/time.c`**

  * 时间子系统封装（基于 platform timer / RTC）。
* **`kernel/console.c`**

  * console 抽象（上接 syscall `read/write`，下接 UART IRQ）。

跨模块依赖：

* **arch/**：提供 trap 入口、`struct trapframe`、CSR 封装、`arch_enable_timer_interrupts()` 等。
* **platform/**：提供 UART / PLIC / RTC / timer 等 board 级服务。
* **user/**：通过 syscall 使用内核能力（线程、IO、时间等）。

---

## 2. 启动流程（`kernel_main`）

典型流程（简化）：

1. **接收 OpenSBI 传入的参数**

   * `hartid`：当前核 ID（目前主要用 0）。
   * `dtb_pa`：设备树物理地址。

2. **平台初始化（board 支持）**

   * `platform_set_dtb(dtb_pa)`
   * `platform_uart_init()`：保证后续 log 可用。
   * `trap_init()`：安装 S 模式 trap 向量。
   * `platform_rtc_init()`：解析 Goldfish RTC。
   * `platform_timer_init(hartid)`：timer 使用 SBI TIME + `time` CSR。
   * `platform_plic_init()`：初始化 PLIC 和 IRQ 注册表。

3. **内核子系统初始化**

   * `console_init()`：console 层（ring buffer + stdin 阻塞读）。
   * `log_init_baremetal()`：log 系统（`pr_info/pr_debug` 等）。
   * `time_init()`：内核时间子系统。
   * `threads_init()`：线程系统 + 调度器。

4. **开启定时器中断 & 启动时钟**

   * `arch_enable_timer_interrupts()`：打开 S 模式 timer 中断。
   * `platform_timer_start_after(DELTA_TICKS)`：编程下一次时间片。
   * `platform_rtc_set_alarm_after(...)`：用于实验 RTC 中断。

5. **启动第一个用户线程**

   * `threads_exec(user_main, NULL)`：启动用户空间入口（shell）。
   * 主核进入循环：`for (;;) platform_idle();`（`wfi`）。

---

## 3. Trap 与中断处理（`kernel/trap.c`）

核心职责：

* 统一入口 `trap_entry_c(struct trapframe *tf)`，由 arch 汇编入口跳转；
* 读取 `scause/stval/sepc/sstatus` 等 CSR；
* 区分：

  * **同步异常（exception）**

    * ecall from U-mode → syscall
    * 其它非法访问/指令 → panic
  * **异步中断（interrupt）**

    * S-mode timer interrupt → 内核定时器/调度
    * S-mode external interrupt → PLIC 分发
    * S-mode software interrupt（以后可用于 IPI）

典型逻辑：

```c
void trap_entry_c(struct trapframe *tf)
{
    uint64_t scause = csr_read(scause);

    if (is_interrupt(scause)) {
        switch (interrupt_code(scause)) {
        case S_TIMER_INTERRUPT:
            handle_s_timer(tf);
            break;
        case S_EXTERNAL_INTERRUPT:
            platform_handle_s_external(tf);
            break;
        ...
        }
    } else {
        if (is_ecall_from_user(scause)) {
            sys_dispatch(tf);   // 根据 a0=SYS_xxx 调用对应 sys_ 函数
        } else {
            // 打印 TRAP 信息并 panic
        }
    }
}
```

特点：

* trap 逻辑只做“分类 + 调用对应子系统”，具体外设处理交给 platform 或 kernel 子模块；
* 对无法处理的异常统一 panic，打印 backtrace，方便调试。

---

## 4. 线程与调度（`kernel/thread.c`）

### 4.1 线程模型（简化）

* `struct thread` 大致包含：

  * `tid`：线程 ID；
  * 状态：`RUNNABLE / BLOCKED / ZOMBIE / UNUSED`；
  * `struct trapframe *tf`：寄存器上下文（用户线程）；
  * 栈指针 / 内核栈指针；
  * 链表指针（挂到 runqueue / wait queue）。

### 4.2 基本 API

* `threads_init()`：初始化全局线程表 + runqueue。
* `threads_exec(entry, arg)`：启动用户空间的主线程（shell）。
* `thread_create(entry, arg, name)`：用于用户态 syscall 封装。
* `thread_exit() / thread_join()`：线程退出 / 等待。

### 4.3 调度

* 在 timer 中断处理函数中：

  * 更新 time slice；
  * 标记当前线程需要切换；
  * 调用 `schedule()`。
* `schedule()`：

  * 从 runqueue 中选择下一个 RUNNABLE 线程；
  * 进行上下文切换（保存当前 tf，切换到下一个线程）。

### 4.4 阻塞与唤醒

* 比如 console stdin：

  * `console_read_block_once()` 中：

    * 如果 ring buffer 无数据：

      * 记录 `g_stdin_waiter = thread_current()`；
      * 调用 `thread_block(tf)` 把当前线程挂起；
  * UART IRQ 中：

    * 收到字符 → `console_on_char_from_irq(ch)`：

      * 放入 ring buffer；
      * 若有 `g_stdin_waiter`，调用 `thread_read_from_stdin(...)` 唤醒等待线程。

---

## 5. Syscall 层（`kernel/syscall.c` + `user/` wrappers）

### 5.1 调用约定

* 用户态通过 `ecall` 触发 syscall：

  * `a0`：syscall 号（`SYS_XXX`）；
  * `a1..aX`：参数；
* trap handler 检测到 “ecall from U-mode”，跳到 `sys_dispatch()`；
* `sys_dispatch()` 根据 `a0` 调用对应 `sys_XXX()`，将返回值再写回 `a0`，最终从 trap 返回到用户态。

### 5.2 主要 syscall

当前已经实现的（部分）：

* 线程相关：

  * `SYS_THREAD_CREATE` → `sys_thread_create(entry, arg, name)`；
  * `SYS_THREAD_JOIN` → 等待另一线程退出；
  * `SYS_THREAD_EXIT` → 终止当前线程。
* IO：

  * `SYS_READ` / `SYS_WRITE` → 走 console / UART。
* 时间：

  * `SYS_CLOCK_GETTIME` → 基于 RTC 的 `platform_rtc_read_ns()`。
* 调试 / 观测：

  * `SYS_IRQ_GET_STATS` → 将平台维护的 IRQ 统计 copy 到用户 buffer，用于 `irqstat` 命令。

用户态用 C 封装：

```c
tid_t thread_create(thread_entry_t entry, void *arg, const char *name);
ssize_t read(int fd, void *buf, size_t len);
int clock_gettime(int clk_id, struct timespec *ts);
long sys_irq_get_stats(struct irqstat_user *buf, size_t n);
```

shell 和其它用户程序只需要操作这些“标准接口”，底下的 trap/syscall/platform 对它们是透明的。

---

## 6. 时间子系统（`kernel/time.c` + RTC）

内核时间由两部分组成：

1. **tick（单调时间）**

   * `platform_time_now()` → 读取 `time` CSR（OpenSBI 配好的 mcounteren）；
   * 用于调度、timer 中断间隔计算等。

2. **真实时间（UTC 时间戳）**

   * `platform_rtc_read_ns()` → 读取 Goldfish RTC（ns since epoch / boot）；
   * sys_clock_gettime() 将其转换成 `struct timespec`；
   * 用户态通过 `epoch_to_utc_datetime()` 转为 `datetime_t`，`date` 命令输出 `YYYY-MM-DD hh:mm:ss`。

Timer 中断使用 SBI TIME：

* `platform_timer_start_at(when)` → 调用 `sbi_set_timer(when)`；
* 由 OpenSBI 在 M 模式写 `mtimecmp`，触发 S 模式 timer 中断。

---

## 7. Console / TTY 层（`kernel/console.c`）

职责：

* 对上：给内核 / 用户态提供统一的 **标准输入输出**；

  * `console_write()` → UART 输出；
  * `console_read_block_once()` / `console_read_nonblock()` → stdin 输入。
* 对下：对接 UART 驱动 + IRQ：

  * UART 中断收到字符 → `console_on_char_from_irq(ch)`：

    * 字符放入 ring buffer；
    * 唤醒等待 stdin 的线程（shell）。
* 结合 syscall：

  * `sys_read()` 调用 console 的阻塞读；
  * `sys_write()` 调用 console 的输出。

这样，shell 的交互完全体现为：

> 用户敲键盘 → UART IRQ → console ring buffer → 唤醒 shell → shell 通过 syscall `read()` 得到数据。

---

## 8. 总体思路与演进方向

总结一下这个内核的“风格”：

* **arch/** 做特权级相关的最低层（CSR / trap 汇编）；
* **platform/** 做“板子支持”，所有硬件细节、FDT 解析都关在这个层里；
* **kernel/** 在这两层之上实现：

  * 线程 / 调度；
  * trap/中断分类；
  * syscall 层抽象；
  * 时间子系统；
  * console / TTY。

目前已经有：

* 用户态 shell；
* 线程 + 阻塞 IO；
* PLIC + UART + RTC 中断；
* 基于 RTC 的 `date`；
* IRQ 统计 + `irqstat` 命令。
