# ============================================================================
#  [legacy] RISC-V S-mode Kernel Playground - Top-level Makefile
#
#  Layout (当前工程结构):
#    arch/riscv/            - 启动代码 & trap 汇编
#    platform/qemu-virt-sbi - 平台相关实现 (S-mode + OpenSBI + UART+TIME)
#    kernel/                - 内核入口 & 将来的调度/线程等
#    lib/                   - 日志等通用库
#    config/                - 全局配置 (log/kernel)
#    linker/                - 链接脚本
# ============================================================================

# Debug / Release 模式
# make RELEASE=YES 切到 Release
RELEASE ?= NO

# CPU：make CPUS=4
CPUS     ?= 4

# ---------------------------------------------------------------------------
# 工具链配置
# ---------------------------------------------------------------------------

CROSS_COMPILE ?= riscv64-unknown-elf-
OPENSBI_CROSS_COMPILE ?= riscv64-unknown-linux-gnu-
#OPENSBI_CROSS_COMPILE ?= riscv64-unknown-elf-

CC      := $(CROSS_COMPILE)gcc
LD      := $(CROSS_COMPILE)gcc
OBJDUMP := $(CROSS_COMPILE)objdump
OBJCOPY := $(CROSS_COMPILE)objcopy
NM      := $(CROSS_COMPILE)nm
SIZE    := $(CROSS_COMPILE)size

UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Darwin)
  GDB := riscv64-elf-gdb
else
  GDB := gdb-multiarch
endif

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
TMP_DIR      := $(BUILD_DIR)/tmp

# Some toolchains use /tmp for intermediate files; keep everything within build/.
export TMPDIR := $(abspath $(TMP_DIR))

TARGET_NAME  := kernel
TARGET       := $(OUT_DIR)/$(TARGET_NAME).elf
TARGET_BIN   := $(OUT_DIR)/$(TARGET_NAME).bin
MAP_FILE     := $(OUT_DIR)/$(TARGET_NAME).map

TARGET_DISASM := $(OUT_DIR)/$(TARGET_NAME).disasm
TARGET_SYMS   := $(OUT_DIR)/$(TARGET_NAME).sym

# ---------------------------------------------------------------------------
# 源文件组织
# ---------------------------------------------------------------------------

ARCH_DIR     := arch/$(ARCH)
PLATFORM_DIR := platform/$(PLATFORM)
KERNEL_DIR   := kernel
USER_DIR     := user
LIB_DIR      := lib
LIBFDT_DIR   := lib/libfdt
CONFIG_DIR   := config
UAPI_DIR     := include/uapi

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
  -I$(LIB_DIR)/include \
	-I$(LIBFDT_DIR) \
	-I$(UAPI_DIR)

# ---------------------------------------------------------------------------
# 编译 / 链接选项
# ---------------------------------------------------------------------------

RISCV_ARCH ?= rv64ima_zicsr
RISCV_ABI  ?= lp64
# QEMU virt 上用 SiFive 7 系列的 tune 很合适
RISCV_TUNE ?= sifive-7-series

# ========================
# 通用 CFLAGS
# ========================
CFLAGS := \
  -march=$(RISCV_ARCH) -mabi=$(RISCV_ABI) -mtune=$(RISCV_TUNE) \
  -ffreestanding -fno-builtin \
  -Wall -Wextra \
  -mcmodel=medany \
  -ffunction-sections -fdata-sections \
  $(INCLUDE_DIRS)

# 强制包含配置头
CFLAGS += -include kernel_config.h

# 自动依赖
CFLAGS  += -MMD -MP
ASFLAGS += -MMD -MP

# smp
CFLAGS += -DMAX_HARTS=$(CPUS)

# ========================
# Debug / Release 特定 CFLAGS
# ========================
ifeq ($(RELEASE),YES)
  CFLAGS += -O2 -DNDEBUG -flto -DKERNEL_BUILD_TYPE=\"release\"
else
  CFLAGS += -g3 -Og -fno-omit-frame-pointer
  CFLAGS += -fno-inline
  CFLAGS += -fno-optimize-sibling-calls
endif

# ========================
# 链接脚本 / LDFLAGS
# ========================
# 链接脚本命名规则：linker/<arch>-<board>.ld
LINKER_SCRIPT := linker/$(ARCH)-$(BOARD).ld

LDFLAGS := \
  -march=$(RISCV_ARCH) -mabi=$(RISCV_ABI) -mtune=$(RISCV_TUNE) \
  -nostdlib -nostartfiles -ffreestanding \
  -Wl,-T,$(LINKER_SCRIPT) \
  -Wl,--gc-sections \
  -Wl,-Map,$(MAP_FILE)

# LTO 在链接阶段也要打开
ifeq ($(RELEASE),YES)
  LDFLAGS += -flto
endif

# ---------------------------------------------------------------------------
# 规则
# ---------------------------------------------------------------------------

.PHONY: all build run disasm-all objdump-objs symbols size

all: build

build: $(TMP_DIR) $(TARGET) $(TARGET_BIN) $(DTS) disasm-all symbols objdump-objs size

# 链接规则：注意先保证目录存在
$(TARGET): $(OBJS)
	@mkdir -p $(dir $@)
	@echo "  LD    $@"
	$(LD) $(CFLAGS) $(OBJS) -o $@ $(LDFLAGS)

$(TARGET_BIN): $(TARGET)
	@echo "  OBJCOPY  $@"
	@mkdir -p $(dir $@)
	$(OBJCOPY) -O binary $< $@

# 可选：统一建几个输出目录（非必须，用了也不会出问题）
$(OUT_DIR) $(OBJ_DIR) $(DUMP_DIR):
	@mkdir -p $@

$(TMP_DIR):
	@mkdir -p $@

# C 源文件编译规则
$(OBJ_DIR)/%.o: %.c | $(TMP_DIR)
	@mkdir -p $(dir $@)
	@echo "  CC    $<"
	$(CC) $(CFLAGS) -c $< -o $@

# 汇编源文件 (.S, 需要预处理) 编译规则
$(OBJ_DIR)/%.o: %.S | $(TMP_DIR)
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
# OpenSBI 集成
# ---------------------------------------------------------------------------

OPENSBI_DIR        := opensbi
OPENSBI_BUILD_DIR  := $(BUILD_DIR)/opensbi
OPENSBI_PLATFORM   := generic

# 我们使用 OpenSBI 的 fw_jump.elf 作为 QEMU 的 BIOS
OPENSBI_FW_JUMP     := $(OPENSBI_BUILD_DIR)/platform/$(OPENSBI_PLATFORM)/firmware/fw_jump.elf
OPENSBI_FW_JUMP_BIN := $(OPENSBI_BUILD_DIR)/platform/$(OPENSBI_PLATFORM)/firmware/fw_jump.bin

# fsbl 需一致
OPENSBI_FW_TEXT_START   ?= 0x80000000
OPENSBI_FW_JUMP_ADDR    ?= 0x80200000
OPENSBI_FW_JUMP_FDT_ADDR?= 0x88000000

OPENSBI_DOCKER_CROSS_COMPILE ?= riscv64-linux-gnu-
OPENSBI_DOCKER_IMAGE ?= opensbi-build:linux-gnu
OPENSBI_DOCKER_WS    ?= /ws
PROJECT_ROOT := $(abspath .)
OPENSBI_DOCKERFILE := $(PROJECT_ROOT)/Dockerfile.opensbi

# ---------------------------------------------------------------------------
# OpenSBI 构建规则
# ---------------------------------------------------------------------------

.PHONY: opensbi docker-opensbi

opensbi: $(TMP_DIR) $(OPENSBI_FW_JUMP)

$(OPENSBI_FW_JUMP):
	@echo "  OPENSBI PLATFORM=$(OPENSBI_PLATFORM) FW_JUMP=y"
	@echo "         FW_TEXT_START=$(OPENSBI_FW_TEXT_START) FW_JUMP_ADDR=$(OPENSBI_FW_JUMP_ADDR) FW_JUMP_FDT_ADDR=$(OPENSBI_FW_JUMP_FDT_ADDR)"
	@mkdir -p $(OPENSBI_BUILD_DIR)
	$(MAKE) -C $(OPENSBI_DIR) \
		PLATFORM=$(OPENSBI_PLATFORM) \
		FW_JUMP=y \
		FW_TEXT_START=$(OPENSBI_FW_TEXT_START) \
		FW_JUMP_ADDR=$(OPENSBI_FW_JUMP_ADDR) \
		FW_JUMP_FDT_ADDR=$(OPENSBI_FW_JUMP_FDT_ADDR) \
			CROSS_COMPILE=$(OPENSBI_CROSS_COMPILE) \
			O=$(abspath $(OPENSBI_BUILD_DIR))

# fw_jump.bin is generated alongside fw_jump.elf; keep it as an explicit target
# so QEMU rules can depend on it.
$(OPENSBI_FW_JUMP_BIN): $(OPENSBI_FW_JUMP)

docker-opensbi: docker-opensbi-image
	@echo "  OPENSBI (docker) PLATFORM=$(OPENSBI_PLATFORM) O=$(OPENSBI_BUILD_DIR)"
	@mkdir -p $(OPENSBI_BUILD_DIR)
	@docker run --rm -t \
		-u "$$(id -u):$$(id -g)" \
		-v "$(PROJECT_ROOT):$(OPENSBI_DOCKER_WS)" \
		-w "$(OPENSBI_DOCKER_WS)" \
		"$(OPENSBI_DOCKER_IMAGE)" \
		bash -lc 'make -C "$(OPENSBI_DIR)" \
			PLATFORM="$(OPENSBI_PLATFORM)" \
			FW_JUMP=y \
			FW_TEXT_START="$(OPENSBI_FW_TEXT_START)" \
			FW_JUMP_ADDR="$(OPENSBI_FW_JUMP_ADDR)" \
			FW_JUMP_FDT_ADDR="$(OPENSBI_FW_JUMP_FDT_ADDR)" \
			CROSS_COMPILE="$(OPENSBI_DOCKER_CROSS_COMPILE)" \
			O="$(OPENSBI_DOCKER_WS)/$(OPENSBI_BUILD_DIR)"'

docker-opensbi-image:
	@docker image inspect "$(OPENSBI_DOCKER_IMAGE)" >/dev/null 2>&1 || ( \
		echo "  DOCKER BUILD $(OPENSBI_DOCKER_IMAGE)"; \
		docker build -t "$(OPENSBI_DOCKER_IMAGE)" -f "$(OPENSBI_DOCKERFILE)" "$(PROJECT_ROOT)"; \
	)

docker-opensbi-image-rebuild:
	@echo "  DOCKER REBUILD $(OPENSBI_DOCKER_IMAGE)"
	@docker build -t "$(OPENSBI_DOCKER_IMAGE)" -f "$(OPENSBI_DOCKERFILE)" "$(PROJECT_ROOT)"

# ---------------------------------------------------------------------------
# QEMU 运行配置
# ---------------------------------------------------------------------------

DTB := $(OUT_DIR)/virt.dtb
DTS := $(OUT_DIR)/virt.dts

QEMU_MACHINE         ?= virt
QEMU_MACHINE_EXTRAS  ?=
QEMU_MEM             ?= 256M
QEMU_COMMON_OPTS     ?= -nographic -m $(QEMU_MEM)
QEMU_SMP_OPTS        ?= -smp $(CPUS)

QEMU      ?= qemu-system-riscv64
QEMU_OPTS = -machine $(QEMU_MACHINE)$(QEMU_MACHINE_EXTRAS) \
            $(QEMU_SMP_OPTS) $(QEMU_COMMON_OPTS)

QEMU_GDB_PORT ?= 1234

# ---------------------------------------------------------------------------
# 运行 / 调试
# ---------------------------------------------------------------------------

.PHONY: qemu debug qemu-dbg gdb

# 启动 QEMU + 自编译 OpenSBI，在 S 模式运行内核
qemu: $(TARGET_BIN) $(DTB) $(DTS) $(OPENSBI_FW_JUMP_BIN)
	@echo "  QEMU  $(TARGET) (OpenSBI: $(OPENSBI_FW_JUMP_BIN))"
	$(QEMU) $(QEMU_OPTS) \
		-bios $(OPENSBI_FW_JUMP_BIN) \
		-dtb $(DTB) \
		-device loader,file=$(TARGET_BIN),addr=$(OPENSBI_FW_JUMP_ADDR)

debug: qemu-dbg

# 调试运行：QEMU 不跑、挂起在 reset，开放 GDB 端口
qemu-dbg: $(TARGET_BIN) $(DTB) $(OPENSBI_FW_JUMP_BIN) disasm-all
	@echo "  QEMU-DBG  $(TARGET) (gdb on port $(QEMU_GDB_PORT))"
	$(QEMU) $(QEMU_OPTS) \
		-bios $(OPENSBI_FW_JUMP_BIN) \
		-dtb $(DTB) \
		-S -gdb tcp::$(QEMU_GDB_PORT) \
		-device loader,file=$(TARGET_BIN),addr=$(OPENSBI_FW_JUMP_ADDR)

gdb: $(TARGET)
	@echo "  GDB   $(TARGET) (target remote :$(QEMU_GDB_PORT))"
	$(GDB) $(TARGET) -ex "target remote :$(QEMU_GDB_PORT)"

$(DTS): $(DTB)
	dtc -I dtb -O dts $< > $@

$(DTB):
	mkdir -p $(OUT_DIR)
	$(QEMU) \
		-machine $(QEMU_MACHINE)$(QEMU_MACHINE_EXTRAS),dumpdtb=$@ \
		$(QEMU_SMP_OPTS) $(QEMU_COMMON_OPTS) -S


# ---------------------------------------------------------------------------
# 调试：打印当前参与构建的源文件
# ---------------------------------------------------------------------------

.PHONY: debug-sources clean-kernel clean clean-opensbi distclean

debug-sources:
	@echo "ARCH_SRCS     = $(ARCH_SRCS)"
	@echo "PLATFORM_SRCS = $(PLATFORM_SRCS)"
	@echo "KERNEL_SRCS   = $(KERNEL_SRCS)"
	@echo "LIB_SRCS      = $(LIB_SRCS)"
	@echo "SRCS          = $(SRCS)"
	@echo "OBJS          = $(OBJS)"

# 让原来的 clean 变成“只清内核”
clean: clean-kernel

# 只清内核相关的构建产物（保留 build/opensbi）
clean-kernel:
	@echo "  CLEAN kernel artifacts"
	rm -rf $(OBJ_DIR) $(DUMP_DIR) $(OUT_DIR) \
	       $(TARGET_DISASM) $(TARGET_SYMS)

# 调用 opensbi 自己的 clean（一般不常用）
clean-opensbi:
	@echo "  CLEAN OpenSBI artifacts"
	$(MAKE) -C $(OPENSBI_DIR) \
		O=$(abspath $(OPENSBI_BUILD_DIR)) \
		clean || true

# 全部干掉：内核 + OpenSBI，一般用于“重来一次”
distclean: clean-kernel
	@echo "  DISTCLEAN kernel + OpenSBI"
	rm -rf $(OPENSBI_BUILD_DIR)
