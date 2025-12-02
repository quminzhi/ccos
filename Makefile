# ============================================================================
#  RISC-V S-mode Kernel Playground - Top-level Makefile
#
#  Layout (当前工程结构):
#    arch/riscv/            - 启动代码 & trap 汇编
#    platform/qemu-virt-sbi - 平台相关实现 (S-mode + OpenSBI + UART+TIME)
#    kernel/                - 内核入口 & 将来的调度/线程等
#    lib/                   - 日志等通用库
#    config/                - 全局配置 (log/kernel)
#    linker/                - 链接脚本
# ============================================================================

# ---------------------------------------------------------------------------
# 工具链配置
# ---------------------------------------------------------------------------

# 交叉编译器前缀 (可在命令行覆盖：make CROSS_PREFIX=riscv64-gnu11-imafd-elf-)
CROSS_PREFIX ?= riscv64-unknown-elf-

CC      := $(CROSS_PREFIX)gcc
LD      := $(CROSS_PREFIX)gcc
OBJDUMP := $(CROSS_PREFIX)objdump
GDB     := gdb-multiarch

# ---------------------------------------------------------------------------
# 目标配置
# ---------------------------------------------------------------------------

ARCH     := riscv
BOARD    := virt
PLATFORM := qemu-virt-sbi

TARGET   := kernel.elf
DUMP     := $(TARGET:.elf=.dump)
KMAP     := $(TARGET:.elf=.map)

# 链接脚本命名规则：linker/<arch>-<board>.ld
LINKER_SCRIPT := linker/$(ARCH)-$(BOARD).ld

# ---------------------------------------------------------------------------
# 源文件组织
# ---------------------------------------------------------------------------

ARCH_DIR     := arch/$(ARCH)
PLATFORM_DIR := platform/$(PLATFORM)
KERNEL_DIR   := kernel
LIB_DIR      := lib

# 架构相关 (启动 & trap 入口)
ARCH_SRCS := \
  $(ARCH_DIR)/start.S \
  $(ARCH_DIR)/trap.S \
  $(ARCH_DIR)/arch.c


# 内核层 (目前只有 main，将来会增加 sched.c / thread.c / trap.c / timer.c 等)
KERNEL_SRCS := \
  $(KERNEL_DIR)/main.c \
  $(KERNEL_DIR)/trap.c \
  $(KERNEL_DIR)/klib.c

# 通用库 (log 系统 & baremetal 绑定)
LIB_SRCS := \
  $(LIB_DIR)/log.c \
  $(LIB_DIR)/log_baremetal.c

# 平台相关 (S-mode + OpenSBI + TIME + UART)
PLATFORM_SRCS := \
  $(PLATFORM_DIR)/platform_sbi.c \
  $(PLATFORM_DIR)/uart_16550.c


SRCS := $(ARCH_SRCS) $(PLATFORM_SRCS) $(KERNEL_SRCS) $(LIB_SRCS)
OBJS := $(SRCS:.c=.o)
OBJS := $(OBJS:.S=.o)

# ---------------------------------------------------------------------------
# 头文件搜索路径
# ---------------------------------------------------------------------------

INCLUDE_DIRS := \
  -Iconfig \
  -I$(ARCH_DIR)/include \
  -Iplatform/include \
  -I$(KERNEL_DIR)/include \
  -I$(LIB_DIR)/include

# ---------------------------------------------------------------------------
# 编译 / 链接选项
# ---------------------------------------------------------------------------

CFLAGS := \
  -march=rv64ima_zicsr -mabi=lp64 \
  -nostdlib -nostartfiles -ffreestanding -fno-builtin \
  -Wall -Wextra -O2 -g \
  -mcmodel=medany \
  $(INCLUDE_DIRS)

CFLAGS += -include kernel_config.h

LDFLAGS := \
  -nostdlib -nostartfiles -ffreestanding \
  -Wl,-T,$(LINKER_SCRIPT) \
  -Wl,--gc-sections \
	-Wl,-Map,$(KMAP)

# ---------------------------------------------------------------------------
# QEMU 运行配置
# ---------------------------------------------------------------------------

QEMU      ?= qemu-system-riscv64
QEMU_OPTS ?= -machine virt -nographic -bios default -m 128M

QEMU_GDB_PORT ?= 1234

# ---------------------------------------------------------------------------
# 规则
# ---------------------------------------------------------------------------

.PHONY: all clean run dump debug-sources

all: $(TARGET)

$(TARGET): $(OBJS)
	@echo "  LD    $@"
	$(LD) $(CFLAGS) $(OBJS) -o $@ $(LDFLAGS)

# C 源文件编译规则
%.o: %.c
	@echo "  CC    $<"
	$(CC) $(CFLAGS) -c $< -o $@

# 汇编源文件 (.S, 需要预处理) 编译规则
%.o: %.S
	@echo "  AS    $<"
	$(CC) $(CFLAGS) -c $< -o $@

# 反汇编到文件，方便用编辑器/less 查看
dump: $(TARGET)
	@echo "  OBJDUMP -> $(DUMP)"
	$(OBJDUMP) -d $(TARGET) > $(DUMP)
	@echo "Disassembly written to $(DUMP)"

# 启动 QEMU + OpenSBI，在 S 模式运行内核
qemu: $(TARGET)
	@echo "  QEMU  $(TARGET)"
	$(QEMU) $(QEMU_OPTS) -kernel $(TARGET)

# 调试运行：QEMU 不跑、挂起在 reset，开放 GDB 端口
qemu-dbg: $(TARGET)
	@echo "  QEMU-DBG  $(TARGET) (gdb on port $(QEMU_GDB_PORT))"
	$(QEMU) $(QEMU_OPTS) -S -gdb tcp::$(QEMU_GDB_PORT) -kernel $(TARGET)

# 在另一个终端里启动 GDB 并连接 QEMU
gdb: $(TARGET)
	@echo "  GDB   $(TARGET) (target remote :$(QEMU_GDB_PORT))"
	$(GDB) $(TARGET) -ex "target remote :$(QEMU_GDB_PORT)"

# 打印当前参与构建的源文件 (调试 Makefile 用)
debug-sources:
	@echo "ARCH_SRCS     = $(ARCH_SRCS)"
	@echo "PLATFORM_SRCS = $(PLATFORM_SRCS)"
	@echo "KERNEL_SRCS   = $(KERNEL_SRCS)"
	@echo "LIB_SRCS      = $(LIB_SRCS)"
	@echo "SRCS          = $(SRCS)"
	@echo "OBJS          = $(OBJS)"

clean:
	@echo "  CLEAN"
	rm -f $(OBJS) $(TARGET) $(DUMP) $(KMAP)
