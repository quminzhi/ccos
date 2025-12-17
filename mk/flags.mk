# Compile / link flags.

RISCV_ARCH ?= rv64ima_zicsr_zifencei
RISCV_ABI  ?= lp64
RISCV_TUNE ?= sifive-7-series

INCLUDE_DIRS := \
  -Iconfig \
  -I$(ARCH_DIR)/include \
  -Iplatform/include \
  -I$(KERNEL_DIR)/include \
  -I$(LIB_DIR)/include \
  -I$(LIBFDT_DIR) \
  -I$(UAPI_DIR)

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

ifeq ($(RELEASE),YES)
  CFLAGS += -O2 -DNDEBUG -flto -DKERNEL_BUILD_TYPE=\"release\"
else
  CFLAGS += -g3 -Og -fno-omit-frame-pointer
  CFLAGS += -fno-inline
  CFLAGS += -fno-optimize-sibling-calls
endif

LINKER_SCRIPT := linker/$(ARCH)-$(BOARD).ld

LDFLAGS := \
  -march=$(RISCV_ARCH) -mabi=$(RISCV_ABI) -mtune=$(RISCV_TUNE) \
  -nostdlib -nostartfiles -ffreestanding \
  -Wl,-T,$(LINKER_SCRIPT) \
  -Wl,--gc-sections \
  -Wl,-Map,$(MAP_FILE)

ifeq ($(RELEASE),YES)
  LDFLAGS += -flto
endif
