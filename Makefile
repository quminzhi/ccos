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
NM      := $(CROSS_COMPILE)nm
SIZE    := $(CROSS_COMPILE)size
GDB     := gdb-multiarch

# ---------------------------------------------------------------------------
# 目标配置
# ---------------------------------------------------------------------------

ARCH     := riscv
BOARD    := virt
PLATFORM := qemu-virt-sbi

# ---------------------------------------------------------------------------
# 目录 / 目标名设置（**所有输出都进 build/**）
# ---------------------------------------------------------------------------

BUILD_DIR    := build
OBJ_DIR      := $(BUILD_DIR)/obj
DUMP_DIR     := $(BUILD_DIR)/dump
OUT_DIR      := $(BUILD_DIR)/out

TARGET_NAME  := kernel
TARGET       := $(OUT_DIR)/$(TARGET_NAME).elf
MAP_FILE     := $(OUT_DIR)/$(TARGET_NAME).map
# 整个程序的总汇编和符号表
TARGET_DISASM := $(BUILD_DIR)/$(TARGET_NAME).disasm
TARGET_SYMS   := $(BUILD_DIR)/$(TARGET_NAME).sym

# ---------------------------------------------------------------------------
# 源文件组织
# ---------------------------------------------------------------------------

BUILD_DIR    := build

ARCH_DIR     := arch/$(ARCH)
PLATFORM_DIR := platform/$(PLATFORM)
KERNEL_DIR   := kernel
USER_DIR     := user
LIB_DIR      := lib
CONFIG_DIR   := config

ARCH_SRCS     := $(shell find $(ARCH_DIR)     -name '*.c' -o -name '*.S')
PLATFORM_SRCS := $(shell find $(PLATFORM_DIR) -name '*.c' -o -name '*.S')
KERNEL_SRCS   := $(shell find $(KERNEL_DIR)   -name '*.c' -o -name '*.S')
USER_SRCS     := $(shell find $(USER_DIR)     -name '*.c' -o -name '*.S')
LIB_SRCS      := $(shell find $(LIB_DIR)      -name '*.c' -o -name '*.S')

SRCS := $(ARCH_SRCS) $(PLATFORM_SRCS) $(KERNEL_SRCS) $(USER_SRCS) $(LIB_SRCS)

OBJS := $(patsubst %.c, $(OBJ_DIR)/%.o, $(SRCS))
OBJS := $(patsubst %.S, $(OBJ_DIR)/%.o, $(OBJS))
DEPS := $(OBJS:.o=.d)
# 每个 .o 对应的一份 objdump 反汇编
OBJ_DUMPS := $(patsubst $(OBJ_DIR)/%.o, $(DUMP_DIR)/%.objdump, $(OBJS))

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
  -Wall -Wextra \
  -mcmodel=medany \
  $(INCLUDE_DIRS)

CFLAGS += -include kernel_config.h

# 自动依赖
CFLAGS += -MMD -MP
ASFLAGS += -MMD -MP

# 调试相关
CFLAGS += -g3 -Og -fno-omit-frame-pointer
CFLAGS += -fno-inline
CFLAGS += -fno-optimize-sibling-calls

# 链接脚本命名规则：linker/<arch>-<board>.ld
LINKER_SCRIPT := linker/$(ARCH)-$(BOARD).ld

LDFLAGS := \
  -nostdlib -nostartfiles -ffreestanding \
  -Wl,-T,$(LINKER_SCRIPT) \
  -Wl,--gc-sections \
	-Wl,-Map,$(MAP_FILE)

# ---------------------------------------------------------------------------
# QEMU 运行配置
# ---------------------------------------------------------------------------

QEMU      ?= qemu-system-riscv64
QEMU_OPTS ?= -machine virt -nographic -bios default -m 128M

QEMU_GDB_PORT ?= 1234

# ---------------------------------------------------------------------------
# 规则
# ---------------------------------------------------------------------------

.PHONY: all clean run debug-sources disasm-all objdump-objs symbols size

all: $(TARGET) disasm-all symbols objdump-objs size

# 链接规则：注意先保证目录存在
$(TARGET): $(OBJS)
	@mkdir -p $(dir $@)
	@echo "  LD    $@"
	$(LD) $(CFLAGS) $(OBJS) -o $@ $(LDFLAGS)

# 可选：统一建几个输出目录（非必须，用了也不会出问题）
$(OUT_DIR) $(OBJ_DIR) $(DUMP_DIR):
	@mkdir -p $@

# C 源文件编译规则
$(OBJ_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "  CC    $<"
	$(CC) $(CFLAGS) -c $< -o $@

# 汇编源文件 (.S, 需要预处理) 编译规则
$(OBJ_DIR)/%.o: %.S
	@mkdir -p $(dir $@)
	@echo "  AS    $<"
	$(CC) $(CFLAGS) -c $< -o $@

# ---------------------------------------------------------------------------
# 自动依赖（在所有规则后面 include）
# ---------------------------------------------------------------------------

-include $(DEPS)

# ---------------------------------------------------------------------------
# 一键生成整个程序的总汇编 & 符号表
#   disasm-all: 整个 ELF 的汇编（含源码）-> build/kernel.disasm
#   symbols:    有序符号表 -> build/kernel.sym
# ---------------------------------------------------------------------------

# 反汇编到文件，方便用编辑器/less 查看
disasm-all: $(TARGET)
	@mkdir -p $(dir $(TARGET_DISASM))
	@echo "  OBJDUMP (full ELF) -> $(TARGET_DISASM)"
	$(OBJDUMP) -d -S -x $(TARGET) > $(TARGET_DISASM)

symbols: $(TARGET)
	@mkdir -p $(dir $(TARGET_SYMS))
	@echo "  NM      (symbols)  -> $(TARGET_SYMS)"
	$(NM) -n $(TARGET) > $(TARGET_SYMS)

size: $(TARGET)
	@echo "  SIZE    $<"
	$(SIZE) $<

# ---------------------------------------------------------------------------
# 为每个 .o 生成反汇编（objdump 风格）
#   objdump-objs: 生成 build/dump/xxx.objdump
# ---------------------------------------------------------------------------

objdump-objs: $(OBJ_DUMPS)

# pattern rule: 单个 .o -> 对应 .objdump
$(DUMP_DIR)/%.objdump: $(OBJ_DIR)/%.o
	@mkdir -p $(dir $@)
	@echo "  OBJDUMP $< -> $@"
	$(OBJDUMP) -d -S $< > $@

# ---------------------------------------------------------------------------
# 运行 / 调试
# ---------------------------------------------------------------------------

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

# ---------------------------------------------------------------------------
# 调试：打印当前参与构建的源文件
# ---------------------------------------------------------------------------

debug-sources:
	@echo "ARCH_SRCS     = $(ARCH_SRCS)"
	@echo "PLATFORM_SRCS = $(PLATFORM_SRCS)"
	@echo "KERNEL_SRCS   = $(KERNEL_SRCS)"
	@echo "LIB_SRCS      = $(LIB_SRCS)"
	@echo "SRCS          = $(SRCS)"
	@echo "OBJS          = $(OBJS)"

clean:
	@echo "  CLEAN"
	rm -rf $(BUILD_DIR)
