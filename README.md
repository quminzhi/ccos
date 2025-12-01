# S-mode Kernel Playground 小结（当前进度）

> 目标：在 **QEMU virt + OpenSBI** 上，用 **RISC-V S 模式** 跑一个迷你内核，并为以后把 M 模式 bare-metal OS 迁移过来打好底层架构。

---

## 1. 整体架构 & 分层

当前工程分 4 层：

1. **arch/**：与 RISC-V 架构 & 特权级强相关的汇编 & CSR 封装
2. **platform/**：与具体“板子/运行环境”相关（这里是 QEMU virt + OpenSBI）
3. **kernel/**：内核逻辑（入口、trap 处理、以后会有调度/线程）
4. **lib/**：通用库（目前主要是 log 系统）

总体依赖关系：

`kernel` → `platform` → `arch` + 硬件

`lib/log` → `platform_write`（不直接碰 UART/SBI）

---

## 2. 目录结构 & 关键文件

### 2.1 arch/riscv/

- `arch/riscv/start.S`

  - OpenSBI 把 S 模式 PC 设为 `0x80200000`，这里是 S 模式入口 `_start`。
  - 工作：
    - 建立栈（使用 .bss 中预留的栈空间）。
    - 清零 `.bss` 段。
    - 调用 `kernel_main(long hartid, long dtb_pa)`（在 `kernel/main.c` 中定义）。

- `arch/riscv/trap.S`

  - 定义通用的 S 模式 trap 入口 `trap_entry`：
    - 保存最小现场（至少保存 `ra`）。
    - 调用 `trap_entry_c`（C 版本 trap handler，在 `kernel/trap.c` 中）。
    - 恢复现场。
    - `sret` 返回。

- `arch/riscv/include/riscv_csr.h`
  - 提供统一的 **CSR 访问接口** 和常用 bit 定义：
    - `reg_t` / `RISCV_XLEN`
    - `csr_read(csr)` / `csr_write(csr, val)`
    - `MSTATUS_*` / `MIE_*` / `MIP_*` 等 M 模式位
    - **S 模式别名**：
      - `SSTATUS_SIE` 等（基于 mstatus 的视图）
      - `SIE_STIE` / `SIP_STIP` 等（基于 mie/mip 的视图）
    - `mcause_is_interrupt(mcause)` / `mcause_code(mcause)`：  
      用于判断 xcause 是中断还是异常，以及取出 code（对 `scause` 一样适用）。
    - 中断 code 枚举：
      - `IRQ_TIMER_S` / `IRQ_TIMER_M` 等，用于区分 S/M 模式定时器中断。

---

### 2.2 platform/

- `platform/include/platform.h`

  平台抽象接口（给 kernel / lib 用）：

```c
  typedef uint64_t platform_time_t;
  typedef void (*platform_timer_handler_t)(void);

  void platform_init(platform_timer_handler_t timer_handler);

  void platform_write(const char *buf, size_t len);
  void platform_puts(const char *s);
  void platform_idle(void);

  platform_time_t platform_time_now(void);
  void platform_timer_start_after(platform_time_t delta_ticks);
  void platform_timer_start_at(platform_time_t when);

  void platform_handle_timer_interrupt(void);
```

设计要点：

- **OS 不直接操作 CSR / SBI / UART**，统一通过这些接口。

- timer 中断发生时，由内核 trap handler 调用 `platform_handle_timer_interrupt()` 再转到内核注册的回调。

- `platform/include/sbi.h`

  - 封装 OpenSBI 标准扩展（特别是 **TIME 扩展** 的 `sbi_set_timer(stime_value)`）。
  - 不再使用 legacy SBI 接口。

- `platform/qemu-virt-sbi/platform_sbi.c`

  针对 **QEMU virt + OpenSBI + S 模式** 的具体实现：

  - 输出相关：

    - `platform_write` → 调用 UART 驱动写原始 buffer。
    - `platform_puts` → 换行处理（`\n` → `\r\n`）后写串口。
    - `platform_idle` → `wfi`。

  - 定时器：

    - `platform_time_now` → 读 `time` CSR（依赖 OpenSBI 配置的 mcounteren）。
    - `platform_timer_start_at` → `sbi_set_timer(when)`。
    - `platform_timer_start_after(dt)` → `now = time`，然后设置 `now + dt`。

  - 初始化：

    - `platform_init(timer_handler)`：

      - `uart16550_init()`（如果有需要的话）。
      - `csr_write(stvec, trap_entry)`：把 S 模式 trap 入口设置为 arch 层提供的 `trap_entry`。
      - 打印 `stvec` 当前设置（便于调试）。
      - 设置 `sstatus.SIE`（打开 S 模式全局中断）。
      - 设置 `sie.STIE`（只打开 S 模式定时器中断）。
      - 记录 `g_timer_handler = timer_handler`，供 timer 中断时调用。

  - timer 中断入口：

    - `platform_handle_timer_interrupt()`：

      - 如果注册了 `g_timer_handler`，就调用；否则打印一行错误。

> 注意：platform 层 **不依赖 log 系统**，只暴露一个纯粹的输出接口 `platform_write`，避免 log ↔ platform 循环依赖。

---

### 2.3 kernel/

- `kernel/include/kernel.h`

  - 声明核心入口：

    ```c
    void kernel_main(long hartid, long dtb_pa);
    ```

- `kernel/main.c`

  - 内核入口（原来的 `main` 逻辑现在变成真正的 “S-mode kernel main”）：

    - 打印启动信息（通过 log）：

      ```c
      pr_info("hello from S-mode, hart=%ld", hartid);
      ```

    - 初始化平台：

      ```c
      platform_init(kernel_timer_tick);
      ```

    - 初始化日志系统（bare-metal S 模式版）：

      ```c
      log_init_baremetal();
      ```

    - 启动一次定时器：

      ```c
      platform_timer_start_after(10000000UL); // ~1s, QEMU 10MHz 假设
      ```

    - 主循环：

      ```c
      for (;;) {
          platform_idle();
      }
      ```

  - `kernel_timer_tick`（示例）：

    - 在 timer 中断里被调用，可以做：

      - 打一条 `pr_debug("tick")`；
      - 重新设置下一次定时器（实现周期性 tick）。

- `kernel/trap.c`

  - `trap_entry_c`：真正的 C 层 trap handler，供 `arch/riscv/trap.S` 调用。

  - 逻辑：

    ```c
    void trap_entry_c(void)
    {
        reg_t scause = csr_read(scause);
        reg_t stval  = csr_read(stval);
        reg_t code   = mcause_code(scause);

        if (mcause_is_interrupt(scause)) {
            if (code == IRQ_TIMER_S) {
                // S 模式定时器中断 → 转给平台处理（再调用内核注册的 timer handler）
                platform_handle_timer_interrupt();
                return;
            }

            // 其他中断：用 pr_err 打印错误并停机
            pr_err("unhandled interrupt: code=%llu scause=0x%llx",
                   (unsigned long long)code,
                   (unsigned long long)scause);
        } else {
            // 异常：打印 scause/stval
            pr_err("exception: code=%llu scause=0x%llx stval=0x%llx",
                   (unsigned long long)code,
                   (unsigned long long)scause,
                   (unsigned long long)stval);
        }

        // 当前阶段：遇到未处理 trap 直接 wfi 死循环
        for (;;) {
            __asm__ volatile("wfi");
        }
    }
    ```

  - 这样：

    - **trap 逻辑属于 kernel 层**（内核控制中断/异常处理策略）；
    - platform 层只做 timer 回调 & 硬件 glue。

---

### 2.4 lib/（日志系统）

- `lib/include/log.h`

  - 通用 logging 框架接口：

    - 日志级别枚举 `LOG_LEVEL_*`；
    - `log_init(log_write_fn_t)` / `log_set_level` / `log_printf`；
    - 前缀宏：`pr_err/pr_warn/pr_info/pr_debug/pr_trace`。

  - 所有日志最终会走一个 `log_write_fn_t` writer，这个在裸机环境下由 `log_baremetal.c` 提供。

- `lib/log.c`

  - 已经改造成纯 **C 版本**，不再依赖 C++ 特性：

    - 用 `<stdbool.h>` 的 `bool`。
    - 没有 `int&` / `extern "C"`.

  - 内部实现一个“小型 printf”：

    - 支持 `%s/%c/%d/%u/%x/%X/%p/%%`；
    - 已扩展支持长度修饰 `l/ll`，所以可以正确处理 `%ld/%llu` 等；
    - 利用 `log_append_*` 系列函数在 buffer 中构造格式化字符串。

  - 还支持：

    - **运行时日志级别过滤**（runtime level）；
    - 可选的 **timestamp** 和 **ring buffer**（由宏控制是否编译）。

- `lib/log_baremetal.c`

  - 将 log 框架挂到 **S 模式 bare-metal 平台** 上：

    - `log_platform_write` → 调 `platform_write(buf, len)`。
    - 如果启用 timestamp：

      - `log_get_time_ms` → `platform_time_now()` / 10000，假设 time CSR 频率约 10MHz，将 tick 粗略转换为毫秒。

    - `log_init_baremetal()`：

      ```c
      void log_init_baremetal(void)
      {
          log_init(log_platform_write);
          log_set_level(LOG_RUNTIME_DEFAULT_LEVEL);
          log_set_path_mode(LOG_PATH_BASENAME);
      ```

  #if LOG_ENABLE_TIMESTAMP
  log_set_timestamp_fn(log_get_time_ms);
  #endif
  pr_info("log: baremetal init (level=%d)", (int)log_get_level());
  }

  ```

  * 这样，所有 `pr_info/pr_err` 会经过：
    `log.c` → `log_platform_write` → `platform_write` → UART → QEMU 控制台。
  ```

---

### 2.5 config/

- `config/log_config.h`

  - 集中定义日志系统相关宏（可以通过这个文件控制整个工程的 logging 行为）：

    ```c
    #define LOG_COMPILE_LEVEL         LOG_LEVEL_DEBUG
    #define LOG_RUNTIME_DEFAULT_LEVEL LOG_LEVEL_DEBUG
    #define LOG_ENABLE_TIMESTAMP      1
    #define LOG_USE_RING_BUFFER       1
    #define LOG_BUFFER_SIZE           256
    #define LOG_RING_BUFFER_SIZE      4096
    ```

- `config/kernel_config.h`

  - 目前主要是把 log 配置拉进来，以后可以扩展更多全局内核配置：

    ```c
    #include "log_config.h"
    // 将来加 KERNEL_MAX_CPUS / KERNEL_ENABLE_SMP 等
    ```

---

### 2.6 linker/ & Makefile

- `linker/riscv-virt.ld`

  - S 模式内核链接脚本：

    - 起始地址：`. = 0x80200000;`（与 OpenSBI `Next Address` 匹配）。
    - 合理划分：

      - `.text` / `.rodata` → RX 段；
      - `.data` / `.bss` → RW 段；

    - 使用 PHDRS 及 `FLAGS(R|W|X)` 避免出现 “LOAD segment with RWX permissions” 的警告。

- 顶层 `Makefile`

  - 使用交叉工具链（`CROSS_PREFIX` 可配置）。
  - `CFLAGS`：

    - `-march=rv64gc -mabi=lp64 -mcmodel=medany`
    - `-nostdlib -nostartfiles -ffreestanding -fno-builtin`
    - `-Wall -Wextra -O2 -g`
    - 包含路径：`config/`, `arch/riscv/include`, `platform/include`, `kernel/include`, `lib/include`

  - 源文件按层组织：

    - `ARCH_SRCS`：`start.S`、`trap.S`
    - `PLATFORM_SRCS`：`platform/qemu-virt-sbi/platform_sbi.c`（以及以后可能的 `uart_16550.c`）
    - `KERNEL_SRCS`：`kernel/main.c`, `kernel/trap.c`
    - `LIB_SRCS`：`lib/log.c`, `lib/log_baremetal.c`

  - 主要目标：

    - `make`：编译生成 `s-mode-kernel.elf`
    - `make run`：使用 `qemu-system-riscv64 -machine virt -nographic -bios default -kernel s-mode-kernel.elf` 启动
    - `make dump`：用 `objdump -d` 生成反汇编 dump
    - `make clean`：清理中间文件

---

## 3. 当前内核运行效果

在 QEMU 下执行 `make run`：

1. OpenSBI 打印固件 banner，表明：

   - Platform: `riscv-virtio,qemu`
   - Domain0 Next Mode: `S-mode`
   - Next Address: `0x0000000080200000`

2. 跳转到 S 模式 `_start` → `kernel_main`：

   - 日志系统初始化（`log_init_baremetal()`）。
   - 打印：

     ```text
     [..] [I] main.c:.. main(): hello from S-mode, hart=0
     ```

   - 平台初始化打印 `stvec` 地址。

3. 定时器每次触发：

   - trap → `trap_entry` (汇编) → `trap_entry_c`（C）
   - 检测到 `IRQ_TIMER_S`：

     - 调 `platform_handle_timer_interrupt()` → 内核注册的 `kernel_timer_tick()`。

   - `kernel_timer_tick()` 里可以选择 `pr_debug("tick")` 并安排下一次定时器。

如果出现未处理的中断或异常：

- `trap_entry_c` 会通过 `pr_err(...)` 打出详细信息（code、scause、stval），然后 `wfi` 死循环，方便调试。

---

## 4. 下一步可以做的事情（方便以后看这份笔记时有方向）

1. **扩展 trap 处理逻辑**

   - 支持 S 模式外部中断（PLIC）；
   - 区分各种异常类型（illegal instruction, load/store fault, ecall 等），按类型分发到不同的内核处理函数。

2. **把原来的 M 模式 bare-metal OS 逻辑迁到 S 模式 kernel/**

   - 线程/调度：`kernel/thread.c`, `kernel/sched.c`；
   - timer & tick 逻辑统一通过 `platform_timer_*`。

3. **增加 U 模式用户态支持**

   - S 模式作为真正内核，U 模式作为用户进程；
   - `ecall` 从 U → S，内核在 `trap_entry_c` 中识别 syscall 并分发。

4. **完善 platform 层**

   - 把 UART 独立成 `uart_16550.c/h`（如果还没拆出来的话）；
   - 将来加 `platform/baremetal-m`，让同一个 kernel 可以在 M 模式 + S 模式两种平台上编译运行（练习不同特权级架构）。

---

这份小结可以当成这阶段的“快照”。
以后如果你一段时间没碰这个项目，回头打开这份 markdown，基本能立刻想起来：

- 工程怎么分层的；
- trap/定时器/日志是怎么接起来的；
- 再往前可以做哪些功能扩展。
