来自 Codex 对话日志

## 背景
- 平台：QEMU virt + OpenSBI，S-mode 内核，无分页。
- 目标：SMP 调度完善，支持 IPI 唤醒；Phase1 做本地抢占，Phase2 做 per-hart runqueue。

## 关键决策
- PLIC 初始化：每 hart 做 S-mode context init；外设 IRQ handler 只注册一套，全局优先级设置在 boot hart。
- Timer/抢占：`DELTA_TICKS=100000`（10ms@10MHz），`SCHED_SLICE_TICKS=5`（~50ms）。每 hart 都设置本地 timer；仅 boot hart 推进 `threads_tick()`/sleep 唤醒；时间片用完才 `schedule()`。
- IPI：使用 SBI IPI（EID 0x735049）逐 hart 发送（mask=1, base=target_hart）；软件中断 SSIP 用于立即 resched/唤醒。
- 调度器 Phase1：保持全局大锁，改为每核硬抢占，解决 secondary 长时间占用 CPU 的问题。
- 调度器 Phase2：引入 per-hart FIFO runqueue，RUNNABLE 线程必须在某个 rq；`schedule()` 只从本地 rq 取任务，当前可运行线程（非 idle）放回本地 rq 尾部；idle 不入 rq。
- 选核策略：默认投递到唤醒者 hart，如有更短 rq 则选最短 rq 的在线 hart；可后续换成更复杂的负载均衡。
- 线程状态机约束：RUNNABLE 当且仅当 on_rq=1；RUNNING 表示 on_rq=0 且 running_hart>=0；同一 tid 任何时刻只在一个 rq；idle tid<MAX_HARTS，不入 rq。

## 实现要点（当前代码）
- 新增：`kernel/include/sched.h`, `kernel/sched.c`（timer/IPI 入口、时间片、pick_target_hart）；`kernel/include/runqueue.h`, `kernel/runqueue.c`（per-hart rq）。
- cpu_t：增加 `need_resched`, `slice_left`。`cpu_init_this_hart()` 完成 sched init。
- trap：IRQ_TIMER_S -> `sched_on_timer_irq`；IRQ_SOFT_S -> `sched_on_ipi_irq`。
- 线程：添加 rq 元数据；`thread_make_runnable()` 统一入队并视情况 kick 目标 hart；`thread_create_*`、`thread_wake`、`threads_tick` 等改用该接口；`schedule()` 使用本地 rq、重置时间片。

## 后续建议
- Phase2 后续优化：拆大锁（更细粒度 rq 锁/状态保护）、添加 need_resched 标志检查而非直接 schedule；完善线程亲和/最小负载选核策略。
- 板级移植：用 DT 获取中断控制器/时钟/CPU 数；确认目标板 OpenSBI 支持 IPI，否则需 M 态自实现 IPI/timer。
