# QEMU + OpenSBI + 多核 PLIC 那个大坑记录

> 关键词：S 模式内核、OpenSBI、QEMU virt、PLIC、多核、Boot HART ≠ 0

---

## 0. 现象回顾

在我们把项目改成多核（`CPUS=4`）之后，出现了一个非常诡异的现象：

- 当 **Boot HART = 0** 时：
  - shell 能正常启动；
  - 能从 stdin 读到键盘输入；
  - UART 中断正常触发；
  - 一切都很丝滑。

- 当 **Boot HART ≠ 0**（比如 1/2/3）时：
  - user_main 里的 shell 启动成功；
  - shell 一直卡在 `read(stdin)`，看起来好像输入被“吃掉了”；
  - `thread_join(shell_tid, &status)` 一直在等；
  - 整个系统只有 idle 线程在跑；
  - UART 输入在终端敲得飞起，但 `uart16550_irq_handler()` **一次都没被调用**。

简单说：  
> 单核没问题、多核 boot hart = 0 没问题、  
> 但 **boot hart ≠ 0** 时，UART 中断直接消失了。

---

## 1. 背景：QEMU virt + OpenSBI + PLIC 多核结构

### 1.1 OpenSBI 的角色

- QEMU virt 上，机器一启动是 M 模式（M-mode）；
- OpenSBI 作为 M 模式固件：
  - 初始化硬件（PLIC、CLINT 等）；
  - 按一定策略选择一个 hart 作为 **Boot HART**；
  - 把这个 Boot HART 切到 S 模式，跳到我们的 `_start` / `kernel_main`；
  - 其他 hart 通常处于 “等待被 HSM 启动” 的状态。

> ✅ **重要：Boot HART 不保证是 0**，是 OpenSBI 抽奖决定的。

### 1.2 QEMU virt 上的 PLIC 概览

- PLIC 支持多 hart、多 privilege level；
- **每个 hart 有两个 context**：
  - M-mode context（给 OpenSBI 用）；
  - S-mode context（给我们的内核用）。
- 每个 context 有：
  - 一个 enable 寄存器块；
  - 一个 threshold 偏移；
  - 一个 claim/complete 偏移。

对 QEMU virt 的典型布局可以近似理解为：

- enable：  
  `ENABLE_BASE + context_id * 0x80`
- threshold/claim：  
  `CONTEXT_BASE + context_id * 0x1000`
- context 排列：`M0, S0, M1, S1, M2, S2, ...`  
  → **S-mode context 的 context_id = 2 * hartid + 1**

我们在代码里用的是“hart0 S-mode 的 offset + stride”的办法算其他 hart 的偏移。

---

## 2. 单核时代的 PLIC 实现（踩坑起点）

一开始我们是在 **单核** 模式下写的 PLIC 驱动：

```c
void plic_init_s_mode(void)
{
  plic_ensure_base();
  if (plic_base == 0) {
    return;  // 没有 PLIC，或者 FDT 解析失败
  }

  // S-mode threshold = 0，允许所有优先级的中断
  w32(PLIC_STHRESHOLD_HART0_OFFSET, 0);

  // 一开始关掉所有 S-mode 使能，具体设备谁需要中断谁自己开
  w32(PLIC_SENABLE_HART0_OFFSET, 0);
}

void plic_enable_irq(uint32_t irq)
{
  if (irq >= 32) return;
  plic_ensure_base();
  if (plic_base == 0) return;

  uint32_t en = r32(PLIC_SENABLE_HART0_OFFSET);
  en |= (1u << irq);
  w32(PLIC_SENABLE_HART0_OFFSET, en);
}

uint32_t plic_claim(void)
{
  plic_ensure_base();
  if (plic_base == 0) return 0;

  return r32(PLIC_SCLAIM_HART0_OFFSET);
}

void plic_complete(uint32_t irq)
{
  if (!irq) return;
  plic_ensure_base();
  if (plic_base == 0) return;

  w32(PLIC_SCLAIM_HART0_OFFSET, irq);
}
````

关键点：

* 所有初始化和访问都使用 **硬编码的 hart0 S-mode offset**：

  * `PLIC_STHRESHOLD_HART0_OFFSET`
  * `PLIC_SENABLE_HART0_OFFSET`
  * `PLIC_SCLAIM_HART0_OFFSET`
* 对于“单核 + Boot HART = 0”的场景，这样是完全没问题的。

然后上层的初始化是这样：

```c
void platform_plic_init(void)
{
  // 1. S-mode PLIC context
  plic_init_s_mode();

  // 2. 开 UART0 RTC 中断
  uint32_t uart_irq = uart16550_get_irq();
  platform_register_irq_handler(uart_irq, uart16550_irq_handler, NULL, "uart0");

  /* RTC removed: no RTC IRQ registration */
}

void platform_init(uintptr_t hartid, uintptr_t dtb_pa)
{
  platform_set_dtb(dtb_pa);

  platform_uart_init();
  platform_rtc_init();
  platform_timer_init(hartid);

  platform_irq_table_init();
  platform_plic_init();  // 只在 primary_main 的那颗 hart 上调用一次
}
```

---

## 3. 多核 + Boot HART ≠ 0 时发生了什么？

设想一个典型情况：
**CPUS=4，Boot HART=1**

1. OpenSBI 把 **hart1** 拉到 S 模式，跳进我们的 `_start → kernel_main`；
2. `primary_main(hartid=1)` 里调用：

   ```c
   platform_init(hartid=1, dtb_pa);
   ```
3. `platform_init()` 调用 `platform_plic_init()`：

   * `plic_init_s_mode()` → **硬编码使用 HART0 的 S-mode context**；
   * `platform_register_irq_handler()` → 内部调用 `plic_set_priority()` 和 `plic_enable_irq()`，
     也都是写 **HART0 的 S-mode enable 寄存器**。

结果：

* **UART / RTC 中断被路由到了“hart0 的 S-mode PLIC context”**；
* 但我们的 shell、线程、timer 等等全都跑在 **hart1** 上；
* `arch_enable_external_interrupts()` 我们只在主核（hart1）上调用，
  hart0 的 SIE/SEIE 是关的；
* 所以：

  * 中断 pending 在 hart0 的 S-mode context 里；
  * 但 hart0 既没有开 S-mode external interrupt，也没有跑调度；
  * `plic_claim()` 在 hart1 上读的是 **hart0 的 claim 寄存器**（硬编码 offset），
    而 hart1 的 context 其 enable/threshold 又没配置；
  * 最终表现为：中断彻底“消失”，shell 永远收不到 stdin。

这就是为什么：

* Boot HART = 0 时：一切正常（硬编码的 hart0 context 正好对上）；
* Boot HART ≠ 0 时：

  * shell 启动成功；
  * sleep + join 正常；
  * 但 stdin 永远阻塞，`uart16550_irq_handler()` 根本没被调。

---

## 4. 正确的多核 PLIC 设计思路

**目标：**

* 不再假设 “只有 hart0 用 S-mode PLIC”；
* 哪个 hart 调用 PLIC API，就操作哪个 hart 的 S-mode context；
* 上层 API 尽量保持不变（`plic_init_s_mode()` 等接口不修改）。

### 4.1 用“hart0 offset + stride × hartid”算出 per-hart offset

我们保留原来的 “hart0 S-mode offset” 宏：

```c
// 来自 plic.h 中的旧宏
#define PLIC_SENABLE_HART0_OFFSET      ...
#define PLIC_STHRESHOLD_HART0_OFFSET   ...
#define PLIC_SCLAIM_HART0_OFFSET       ...
#define PLIC_PRIORITY_OFFSET           ...
```

然后在 `plic.c` 内部加几段 helper：

```c
enum {
  PLIC_CONTEXTS_PER_HART          = 2u,      // M + S
  PLIC_ENABLE_PER_CONTEXT_STRIDE  = 0x80u,   // 每个 context 的 enable 块大小
  PLIC_CONTEXT_STRIDE             = 0x1000u, // 每个 context 的 threshold/claim 块大小
};

static inline uint32_t plic_senable_offset_for_hart(uint32_t hartid)
{
  uint32_t delta_ctx = hartid * PLIC_CONTEXTS_PER_HART;
  return PLIC_SENABLE_HART0_OFFSET +
         delta_ctx * PLIC_ENABLE_PER_CONTEXT_STRIDE;
}

static inline uint32_t plic_sthreshold_offset_for_hart(uint32_t hartid)
{
  uint32_t delta_ctx = hartid * PLIC_CONTEXTS_PER_HART;
  return PLIC_STHRESHOLD_HART0_OFFSET +
         delta_ctx * PLIC_CONTEXT_STRIDE;
}

static inline uint32_t plic_sclaim_offset_for_hart(uint32_t hartid)
{
  uint32_t delta_ctx = hartid * PLIC_CONTEXTS_PER_HART;
  return PLIC_SCLAIM_HART0_OFFSET +
         delta_ctx * PLIC_CONTEXT_STRIDE;
}
```

### 4.2 把 “当前 hart” 引入 PLIC API 内部

利用我们已有的函数：

```c
uint32_t cpu_current_hartid(void);
```

我们把原来的 PLIC API 改写成“对当前 hart 生效”。

#### 4.2.1 初始化当前 hart 的 S-mode context

```c
void plic_init_s_mode(void)
{
  plic_ensure_base();
  if (plic_base == 0) {
    return;
  }

  uint32_t hartid = cpu_current_hartid();
  uint32_t th_off = plic_sthreshold_offset_for_hart(hartid);
  uint32_t en_off = plic_senable_offset_for_hart(hartid);

  // S-mode threshold = 0，允许所有优先级的中断
  w32(th_off, 0);

  // 一开始关掉所有 S-mode 使能，具体设备谁需要中断谁自己开
  w32(en_off, 0);
}
```

#### 4.2.2 为当前 hart 打开 / 关闭某个 IRQ

```c
void plic_enable_irq(uint32_t irq)
{
  if (irq >= 32) return;
  plic_ensure_base();
  if (plic_base == 0) return;

  uint32_t hartid = cpu_current_hartid();
  uint32_t en_off = plic_senable_offset_for_hart(hartid);

  uint32_t en = r32(en_off);
  en |= (1u << irq);
  w32(en_off, en);
}

void plic_disable_irq(uint32_t irq)
{
  if (irq >= 32) return;
  plic_ensure_base();
  if (plic_base == 0) return;

  uint32_t hartid = cpu_current_hartid();
  uint32_t en_off = plic_senable_offset_for_hart(hartid);

  uint32_t en = r32(en_off);
  en &= ~(1u << irq);
  w32(en_off, en);
}
```

#### 4.2.3 claim / complete 也变成 per-hart

```c
uint32_t plic_claim(void)
{
  plic_ensure_base();
  if (plic_base == 0) return 0;

  uint32_t hartid = cpu_current_hartid();
  uint32_t cl_off = plic_sclaim_offset_for_hart(hartid);

  return r32(cl_off);
}

void plic_complete(uint32_t irq)
{
  if (!irq) return;
  plic_ensure_base();
  if (plic_base == 0) return;

  uint32_t hartid = cpu_current_hartid();
  uint32_t cl_off = plic_sclaim_offset_for_hart(hartid);

  w32(cl_off, irq);
}
```

#### 4.2.4 全局 IRQ 注册表 `platform.c` 基本不用改

IRQ handler 表：

```c
static irq_entry_t s_irq_table[MAX_IRQ];
static irq_stat_t  s_irq_stats[MAX_IRQ];
static const char *s_irq_name[MAX_IRQ];
```

这一套是**全局的**，没问题。
中断处理流程：

```c
void platform_handle_s_external(struct trapframe* tf)
{
  (void)tf;
  for (;;) {
    uint32_t irq = plic_claim();  // ← 现在根据“当前 hart”的 context claim
    if (!irq) break;

    // 统计 + 查 handler + 调用 + plic_complete(irq)
  }
}
```

逻辑保持不变，只是现在 claim/complete 都在当前 hart 的 S-mode context 上进行。

---

## 5. 修复之后的行为变化

改完之后：

* 无论 OpenSBI 把 **哪一个 hart** 选为 Boot HART：

  * `primary_main` 在那颗 hart 上跑；
  * `platform_init()` 在那颗 hart 上执行；
  * `platform_plic_init()` 调用 `plic_init_s_mode()` / `plic_enable_irq()` 时，
    都会针对“当前 hart”配置 S-mode PLIC context；
  * `arch_enable_external_interrupts()` 也在同一颗 hart 上打开 S-mode 外部中断。

因此：

* UART/RTC 等中断也被路由到了这颗 hart 的 S-mode context；
* shell 所在线程可以正常收到 stdin；
* Boot HART=0/1/2/3 的行为彻底一致。

---

## 6. 总结 & 教训

1. **多核 + PLIC 必须意识到“per-hart context”**

   * 单核时代硬编码 hart0 是没问题的；
   * 一旦 Boot HART 可以不是 0，或者多个 hart 都跑 S-mode，就必须按 hart 计算 offset。

2. **“当前 hart”的概念要贯穿驱动层**

   * `plic_init_s_mode()` / `plic_enable_irq()` / `plic_claim()` 等 API 最终都要基于 `cpu_current_hartid()`；
   * 平台初始化只需要传 hartid 或在 CPU init 时设置 tp/hartid，驱动层自己决定怎么用。

3. **bug 表现往往在更上层暴露**

   * 表面上看是 “shell 卡住”、“stdin 阻塞”、“只有 idle 在跑”；
   * 实际根因是在 PLIC 初始化写死 hart0，导致中断被送到错误的 context 上。

4. **以后给自己的 checklist**

* 写任何和中断/定时器/IPC 相关的东西时，问自己：

  * *“这玩意在多核下是 per-hart 的吗？”*
  * *“我是不是不小心写死了 hart0？”*

* 如果出现：

  * 单核 OK、多核挂；
  * Boot HART=0 OK、Boot HART≠0 挂；
    → 先检查：

    * `mhartid` / hartid 的使用；
    * PLIC 或 CLINT 等是否硬编码了 “hart0”。

---

写到这里，这个坑以后再踩到你就可以一巴掌糊自己脑门了：

> “啊，这不是典型的 **‘所有东西都写到 HART0 上’** 那个坑嘛！” 😂
