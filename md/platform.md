# Platform 层小结（QEMU virt + OpenSBI + RISC-V S-mode 内核）

> 目标：**把“板子相关的东西”都关在 platform/**，让 kernel 只看到一组干净的抽象接口。

---

## 1. 整体架构回顾

当前工程的大致分层：

* **arch/**

  * CSR 封装、trap 入口、上下文切换、`trap_entry` 等
* **platform/**（我们这次的主角）

  * QEMU virt + OpenSBI 下的 **板级支持（BSP）**
  * FDT / libfdt
  * UART 16550、PLIC、Goldfish RTC、timer（基于 SBI）等
* **kernel/**

  * 调度、线程、syscall、time 子系统等
* **user/**

  * shell、用户态库（`u_printf`、`clock_gettime`、`date`、`irqstat` 等）

设计原则：

* **内核不关心 MMIO 地址、IRQ 号、compatible 字符串**；
* 这些全部通过 **设备树 FDT + platform/** 来解析和抽象。

---

## 2. `platform.h`：对内核暴露的 API

简化后的核心接口大概是：

```c
struct trapframe;

typedef uint64_t platform_time_t;

/* DTB */
const void *platform_get_dtb(void);
void        platform_set_dtb(uintptr_t dtb_pa);

/* Console / UART */
void platform_uart_init(void);
void platform_putc(char c);
void platform_puts(const char *s);
void platform_write(const char *buf, size_t len);
void platform_put_hex64(uint64_t x);
void platform_put_dec_s(int64_t v);
void platform_put_dec_us(uint64_t x);

/* RTC（Goldfish RTC） */
void     platform_rtc_init(void);
uint64_t platform_rtc_read_ns(void);
void     platform_rtc_set_alarm_after(uint64_t delay_ns);

/* Timer（时间片、调度 timer） */
void            platform_timer_init(uintptr_t hartid);
platform_time_t platform_time_now(void);                // 基于 time CSR
void            platform_timer_start_at(platform_time_t when);
void            platform_timer_start_after(platform_time_t delta);

/* PLIC / IRQ */
void platform_plic_init(void);

typedef void (*platform_irq_handler_t)(uint32_t irq, void *arg);
void platform_register_irq_handler(uint32_t irq,
                                   platform_irq_handler_t handler,
                                   void *arg,
                                   const char *name);

void platform_handle_s_external(struct trapframe *tf);

/* CPU idle（wfi 封装） */
void platform_idle(void);
```

特点：

* 上层看不到任何硬编码的地址 / IRQ 数字；
* 所有“跑在 QEMU virt + OpenSBI 上”的细节都藏在 platform 内部。

---

## 3. 启动流程：platform 在 boot 时做什么？

### 3.1 `kernel_main()` 中的典型顺序

大致形式（伪代码）：

```c
void kernel_main(long hartid, long dtb_pa)
{
    platform_set_dtb(dtb_pa);

    platform_uart_init();          // console 输出先搞好

    trap_init();                   // 安装 stvec，准备好 trap 入口

    platform_rtc_init();           // FDT 解析 goldfish-rtc reg+irq
    platform_timer_init(hartid);   // Timer（内部目前仍用 SBI TIME）
    platform_plic_init();          // 初始化 PLIC & IRQ 表

    platform_puts("Booting...\n");

    console_init();
    log_init_baremetal();
    time_init();
    threads_init();

    arch_enable_timer_interrupts();           // 开 S-mode timer 中断
    platform_timer_start_after(DELTA_TICKS);  // 编程下一次调度时间片

    platform_rtc_set_alarm_after(3ULL * 1000 * 1000 * 1000); // RTC 实验

    // 启动用户态 shell 线程
    threads_exec(user_main, NULL);

    for (;;)
        platform_idle();   // wfi
}
```

**要点：**

* **先 `set_dtb` 再 init 各个外设**，保证 FDT 可用；
* `trap_init()` 尽量只做 trap 向量安装，不乱覆盖 `sie/sstatus`；
* 中断总开关由 `arch_enable_*` 负责，避免互相踩 bit。

---

## 4. 设备与子系统

### 4.1 FDT & libfdt

* 在 OpenSBI 下，S-mode 内核通过 `a1 = dtb_pa` 拿到 FDT 物理地址；

* `platform_set_dtb(dtb_pa)` 存起来；

* 通过封装好的 helper，比如：

  ```c
  int fdt_find_reg_by_compat(const void *fdt,
                             const char *compat,
                             uint64_t *base,
                             uint64_t *size);
  ```

* 用 `compatible` 查找：

  * UART：`"ns16550a"`
  * PLIC：`"riscv,plic0"` / `"sifive,plic-1.0.0"`
  * RTC：`"google,goldfish-rtc"` 等

FDT 成为“所有 MMIO base、IRQ 号、设备存在与否”的**唯一来源**。

---

### 4.2 UART 16550 + console 层

* UART：

  * 从 FDT 中找到 compatible `ns16550a` 的 reg + irq；
  * 初始化 MMIO base；
  * 通过 `platform_register_irq_handler(uart_irq, handler, "uart0")` 注册；

* `uart_irq_handler()`：

  * 读 IIR/LSR/RBR，取出接收到的字符；
  * 调用 `console_on_char_from_irq(ch)`。

* console 层（在 `kernel/console.c`）：

  * 有 ring buffer + 阻塞 read 支持；
  * IRQ 上下文只负责塞 ring + 唤醒等待 stdin 的线程；
  * syscall `read()` 利用 `console_read_block_once()` 挂起 shell 线程，等 IRQ 唤醒。

**结果**：串口输入真正走的是“**中断驱动 + 线程调度**”，不是忙等。

---

### 4.3 Goldfish RTC：真实世界时间 / alarm

* 从 FDT 解析 compatible `"google,goldfish-rtc"` 的 reg + irq；

* 封装 API：

  * `platform_rtc_read_ns()` → 64bit ns since boot/epoch；
  * `platform_rtc_set_alarm_after(ns)` → 设置下一次 RTC 中断；

* PLIC 上为 RTC 注册 handler：

  ```c
  platform_register_irq_handler(rtc_irq, rtc_irq_trampoline, "rtc0");
  ```

* 内核 time 子系统上层做：

  * `clock_gettime()`（用户态）通过 syscall 拿到当前 ns；
  * `epoch_to_utc_datetime()` 完成 ns → 年月日时分秒 转换；
  * `date` 命令负责打印人类可读时间。

---

### 4.4 Timer：目前使用 SBI TIME + `time` CSR

* QEMU virt + OpenSBI 的架构下：

  * **CLINT/ACLINT MMIO 只允许 M 模式访问**，S 模式直接读写会触发 access fault；
  * 正宗做法：S 模式读 `time` CSR，写 SBI `set_timer`，由 OpenSBI 在 M 模式写 `mtimecmp`。

* 我们当前实现：

  ```c
  platform_time_t platform_time_now(void)
  {
      return csr_read(time);           // time CSR, OpenSBI 已放开 mcounteren
  }

  void platform_timer_start_at(platform_time_t when)
  {
      sbi_set_timer(when);             // SBI TIME 扩展
  }

  void platform_timer_start_after(platform_time_t delta)
  {
      platform_time_t now = platform_time_now();
      platform_timer_start_at(now + delta);
  }
  ```

* 之前尝试过“直接用 CLINT MMIO 后端”，验证出在 S 模式下会触发 `Load access fault`，因此回退为：

  ➜ **FDT 只用于发现 CLINT/ACLINT 的存在，但真正编程 timer 依然通过 SBI。**

---

### 4.5 PLIC + IRQ 子系统

**目标**：让 PLIC 对上表现为“通用 IRQ 分发表”，而不是到处硬编码 `if (irq == UART_IRQ)`。

关键数据结构（platform 内部）：

```c
#define MAX_IRQ 64

typedef void (*platform_irq_handler_t)(uint32_t irq, void *arg);

typedef struct {
  platform_irq_handler_t handler;
  void                  *arg;
} irq_entry_t;

typedef struct {
  uint64_t        count;
  platform_time_t first_tick;
  platform_time_t last_tick;
  platform_time_t max_delta;
} irq_stat_t;

static irq_entry_t  s_irq_table[MAX_IRQ];
static irq_stat_t   s_irq_stats[MAX_IRQ];
static const char  *s_irq_name[MAX_IRQ];
```

注册接口：

```c
void platform_register_irq_handler(uint32_t irq,
                                   platform_irq_handler_t handler,
                                   void *arg,
                                   const char *name)
{
  if (irq >= MAX_IRQ) return;

  s_irq_table[irq].handler = handler;
  s_irq_table[irq].arg     = arg;
  s_irq_name[irq]          = name;

  // PLIC 自身配置
  plic_set_priority(irq, 1);
  plic_enable_irq(irq);
}
```

S 外部中断总 handler：

```c
void platform_handle_s_external(struct trapframe *tf)
{
  (void)tf;

  for (;;) {
    uint32_t irq = plic_claim();
    if (!irq) break;

    platform_time_t now = platform_time_now();

    // 统计信息（中断计数 + 时间）
    if (irq < MAX_IRQ) {
      irq_stat_t *st = &s_irq_stats[irq];
      if (st->count == 0) {
        st->first_tick = now;
      } else {
        platform_time_t delta = now - st->last_tick;
        if (delta > st->max_delta) st->max_delta = delta;
      }
      st->last_tick = now;
      st->count++;
    }

    // 分发到真正 handler
    platform_irq_handler_t handler = NULL;
    void *arg = NULL;
    if (irq < MAX_IRQ) {
      handler = s_irq_table[irq].handler;
      arg     = s_irq_table[irq].arg;
    }

    if (handler) {
      handler(irq, arg);
    } else {
      platform_puts("unknown PLIC irq\n");
    }

    plic_complete(irq);
  }
}
```

➜ UART / RTC / 未来的 VirtIO 之类的 driver 不需要再关心 PLIC 细节，只要注册自己的 IRQ handler 即可。

---

## 5. IRQ 统计与调试：`irqstat` 命令

为方便调试，我们在 platform 层做了 IRQ 统计，并通过 syscall 开放给用户态：

### 5.1 内核侧：snapshot

platform 提供：

```c
typedef struct {
  uint32_t        irq;
  uint64_t        count;
  platform_time_t first_tick;
  platform_time_t last_tick;
  platform_time_t max_delta;
  const char     *name;
} platform_irq_stat_t;

size_t platform_irq_get_stats(platform_irq_stat_t *out, size_t max);
```

内核 syscall 把数据整理成用户态能直接用的结构：

```c
#define IRQSTAT_MAX_NAME 16
#define IRQSTAT_MAX_IRQ  64

struct irqstat_user {
  uint32_t irq;
  uint32_t _pad;
  uint64_t count;
  uint64_t first_tick;
  uint64_t last_tick;
  uint64_t max_delta;
  char     name[IRQSTAT_MAX_NAME];
};
```

syscall handler `sys_irq_get_stats(ubuf, n)`：

* 调 `platform_irq_get_stats()` 拿到内核版数据；
* 把 `name` 截断拷贝到固定长度数组里；
* 返回实际填入的条数。

### 5.2 用户态：`irqstat` shell 命令

用户态通过 `ecall` 封装：

```c
long sys_irq_get_stats(struct irqstat_user *buf, size_t n);
```

`shell` 里的 `cmd_irqstat`：

```c
static struct irqstat_user g_irqstat_buf[IRQSTAT_MAX_IRQ];

static void cmd_irqstat(int argc, char **argv)
{
  (void)argc;
  (void)argv;

  long n = sys_irq_get_stats(g_irqstat_buf, IRQSTAT_MAX_IRQ);
  if (n < 0) {
    u_printf("irqstat: syscall failed (%ld)\n", n);
    return;
  }

  u_printf("irq  count       last_time              max_delta(us)   name\n");

  for (long i = 0; i < n; ++i) {
    const struct irqstat_user *st = &g_irqstat_buf[i];
    if (st->count == 0) continue;

    uint64_t last_ns  = st->last_tick;
    uint64_t last_sec = last_ns / 1000000000ULL;

    datetime_t dt;
    epoch_to_utc_datetime((time_t)last_sec, &dt);

    uint64_t delta_us = st->max_delta / 1000ULL;

    const char *name = st->name[0] ? st->name : "-";

    u_printf("%3u  %8llu  %04d-%02d-%02d %02d:%02d:%02d  %10llu  %s\n",
             (unsigned)st->irq,
             (unsigned long long)st->count,
             dt.year, dt.month, dt.day,
             dt.hour, dt.min, dt.sec,
             (unsigned long long)delta_us,
             name);
  }
}
```

典型输出示例：

```text
> irqstat
irq  count       last_time              max_delta(us)   name
 10       123  2025-12-09 14:23:15          120000      uart0
 11         3  2025-12-09 14:23:17          3000000     rtc0
```

这样就可以非常直观地观察：

* 哪些 IRQ 频率高；
* 最近一次是什么时候；
* 最长间隔大概多长。

---

## 6. 后续可以演进的方向（platform 视角）

在当前 platform 层基础上，接下来可以玩的：

1. **IRQ trace ring buffer + `irqtrace` 命令**

   * 记录最近 N 次 IRQ（时间戳 + irq + hart）；
   * 用于分析 irq 风暴、时序问题。

2. **多核 PLIC 支持**

   * per-hart 的 `SCLAIM` / `SENABLE` offset；
   * `platform_register_irq_handler` 增加目标 hart，以后可以做 irq 亲和。

3. **SBI IPI + SMP**

   * 使用 SBI IPI 接口，在 S-mode 收到 software interrupt，当作 IPI；
   * 配合 IRQ 子系统做简单的跨核唤醒 / 调度。

4. **更正式的时间子系统**

   * 在 kernel/time.c 抽象出 `ktime_get_real_ns()` / `ktime_get_mono_ns()`；
   * platform 只提供底层时间源（RTC / CSR / SBI），内核统一管理“时间”。

---

这就是目前 **platform 层的状态 + 设计思路** 的小总结。
