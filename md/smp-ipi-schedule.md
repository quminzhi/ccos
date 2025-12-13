# SMP 启动与 IPI 调度设计小结

面向开发/回顾：当前多核启动、调度与 IPI 使用方式的设计要点与关键细节。

## 启动流程（boot path）
- **Boot hart 由 OpenSBI 决定**（不保证是 0），直接跳入 `_start` → `kernel_main`（S 模式）。
  - `_start`：清 BSS、设置 boot hart 栈顶、写 `sscratch = cpu*`，然后跳到 `per_hart_start`。
  - `secondary_entry`：供 `sbi_hart_start(h, secondary_entry, dtb_pa)` 启用其它 hart，跳过清 BSS。
- 栈与 per-CPU 指针：
  - 栈数组：`g_kstack[MAX_HARTS][KSTACK_SIZE]`，每 hart 4 KiB（配置可调）。
  - `tp` 永远指向该 hart 的 `cpu_t`，`sscratch` 同步指向 `cpu_t`。
- `cpu_init_this_hart` 关键字段：`hartid`、`kstack_top`、`cur_tf`、`idle_tid`、`timer_irqs`、`ctx_switches`，以及在线标记 `online`。
- 线程资源：
  - 全局线程表 `g_threads[THREAD_MAX]` + 栈 `g_thread_stacks[THREAD_MAX][THREAD_STACK_SIZE]`。
  - 约束：`THREAD_MAX >= MAX_HARTS + 1`，预留 `tid == hartid` 的 idle。
- 进入 idle：
  - `cpu_enter_idle` 将 `idle->tf` 绑定到 `cpu.cur_tf`，状态置为 RUNNING。
  - boot hart 负责启动第一次 timer；所有 hart 打开 `SSIP/SEIP/STIP`（软件/外部/定时器中断）。
- 启停同步：
  - `g_boot_hartid` 记录 boot hart；`smp_boot_done`/`wait_for_smp_boot_done()` 控制多核启动完成前的等待。

## 定时器职责
- **当前仅 boot hart 编程周期性定时器**，避免多源定时器的复杂度。
- 触发链路：`timer` 中断 → `trap_entry_c` → `platform_handle_timer_interrupt()` → `kernel_timer_tick()` → `threads_tick()`.
- `threads_tick()`：
  - `g_ticks++`
  - 将 `wakeup_tick <= g_ticks` 的 SLEEPING 线程置为 RUNNABLE。
  - 若有唤醒，调用 `sched_notify_runnable()` 触发 IPI，唤醒可能 WFI 的其它 hart。
- 改进空间：未来可改为 per-hart timer，降低唤醒抖动、分摊 tick 负载；但需注意 per-hart 调度状态一致性与计时来源。

## 调度模型
- 数据结构：共享单一 `g_threads[]`，无 per-hart run queue。
- 算法：`schedule(tf)` 以当前 tid 为起点，向后扫描 RUNNABLE（跳过 idle 区间）；找不到则继续跑当前 RUNNABLE，否则退回本 hart 的 idle。
- 上下文记账：
  - 调入：`thread_mark_running` 记录 `running_hart`、更新 `runs`，若 `last_hart != current` 则递增 `migrations`。
  - 调出：`thread_mark_not_running` 把 `running_hart` 写入 `last_hart` 后清为 -1，确保 monitor 的 CPU/LAST 精确。
  - `runs`、`migrations` 对 monitor/调试有用，可用于后续负载均衡策略。
- 线程状态转换：
  - `thread_sys_sleep/yield` 将线程置 SLEEPING（或仅让出 CPU），设置 `wakeup_tick`，并调用 `schedule`。
  - `thread_wake/threads_tick` 将目标置 RUNNABLE 并 `sched_notify_runnable()`。
  - idle 线程永不进入用户态，`tid == hartid`，由 `cpu_enter_idle` 初始化。

## IPI 策略（SBI v0.2+ 标量 hart mask）
- 接口：`sbi_send_ipi(unsigned long hart_mask, unsigned long hart_mask_base)`。
  - 我们用 **标量 mask**：`sbi_send_ipi(1UL, target_hart)`，`hart_mask_base = target_hart`，bit0 命中该 hart。
  - 这样避免 v0.1 风格的指针 mask 误传，确保兼容 OpenSBI 1.7 的参数校验。
- 触发时机：
  - boot hart 在 `threads_tick()` 唤醒任意线程后发送 IPI。
  - 其它路径凡是使线程变 RUNNABLE（unblock/create）也通过 `sched_notify_runnable()` 发送 IPI。
- 期望效果：
  - WFI 的 hart 收到 SSIP 后进入调度。
  - RUNNING 的 hart 在下一次 trap 边界也能看到挂起的 SSIP 并调度。
- 依赖：`arch_enable_software_interrupts()` 在每个 hart 已开启；OpenSBI 提供 `SBI_EXT_IPI` 实现。

## 关键假设与约束
- 编译期：`MAX_HARTS`、`KSTACK_SIZE`、`THREAD_STACK_SIZE` 需匹配硬件/内存约束；`THREAD_MAX >= MAX_HARTS + 1`。
- 线程编号：idle tid == hartid，用户/内核线程 tid 从 `FIRST_TID` 起分配。
- 中断路由：设备中断可能配置到多 hart，取决于 PLIC 使能；当前定时器只由 boot hart 编程。
- 共享结构：使用全局 run queue，未来 hart 数上升时需关注锁争用与可扩展性。

## 已知局限 / 可演进方向
- Timer 单核：可演进为 per-hart timer，减少唤醒延迟并分摊 tick；需设计 per-hart 计时与线程超时的一致性。
- Run queue 全局：多核高并发下可能出现锁争用；可引入 per-hart queue + work stealing。
- IPI 逐个发送：现在“一次一个 bit”便于读懂；如果唤醒目标多，可构造多 bit 掩码减少 ECALL 次数。
- 负载均衡：当前仅 round-robin，无亲和/负载感知；可基于 `migrations`、`runs` 做简单 balance/affinity。

## 调试检查单
- 非 0 boot hart 时中断“消失”：
  - 确认 IPI 传参是标量 mask，`hart_mask_base` 对准目标 hart。
  - 确认 secondary entry 后每个 hart 均已打开 SSIP/SEIP/STIP。
- monitor 观测：
  - `CPU` 列：当前运行的 hart 或 `---`（不在跑）。
  - `LAST` 列：上一次运行的 hart；跨 hart 迁移时 `MIG` 递增。
  - 如果 CPU/LAST 异常一致，检查 `thread_mark_not_running` 是否被调用、`running_hart` 是否及时清零。
