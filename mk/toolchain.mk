# Toolchain configuration.

CROSS_COMPILE ?= riscv64-unknown-elf-

# OpenSBI toolchain (typically linux-gnu).
OPENSBI_CROSS_COMPILE ?= riscv64-unknown-linux-gnu-

CC      := $(CROSS_COMPILE)gcc
LD      := $(CROSS_COMPILE)gcc
OBJDUMP := $(CROSS_COMPILE)objdump
OBJCOPY := $(CROSS_COMPILE)objcopy
STRIP   := $(CROSS_COMPILE)strip
NM      := $(CROSS_COMPILE)nm
SIZE    := $(CROSS_COMPILE)size

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
  GDB := riscv64-elf-gdb
else
  GDB := gdb-multiarch
endif

# ---- Diagnostics (mk/common.mk helper targets) ----
PRINT_CONFIG_VARS := BUILD RELEASE BOARD PLATFORM CROSS_COMPILE OPENSBI_CROSS_COMPILE BUILD_DIR OUT_DIR
TOOLCHAIN_PREFIXES := $(CROSS_COMPILE) $(OPENSBI_CROSS_COMPILE)
TOOLCHAIN_TOOLS := gcc ld objdump objcopy nm size
