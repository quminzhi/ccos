# Directory / target names. All outputs go under OUT (default: out/ccos/<board>).

OUT ?= $(call out_dir,ccos,$(BOARD))
BUILD_DIR ?= $(OUT)
OBJ_DIR   := $(BUILD_DIR)/obj
DUMP_DIR  := $(BUILD_DIR)/dump
OUT_DIR   := $(BUILD_DIR)/out
TMP_DIR   := $(BUILD_DIR)/tmp

# Some toolchains use /tmp for intermediate files; keep everything within build/.
export TMPDIR := $(abspath $(TMP_DIR))

TARGET_NAME   := kernel
TARGET        := $(OUT_DIR)/$(TARGET_NAME).elf
TARGET_BIN    := $(OUT_DIR)/$(TARGET_NAME).bin
MAP_FILE      := $(OUT_DIR)/$(TARGET_NAME).map
TARGET_DISASM := $(OUT_DIR)/$(TARGET_NAME).disasm
TARGET_SYMS   := $(OUT_DIR)/$(TARGET_NAME).sym

# Source layout.
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

OBJ_DUMPS := $(patsubst $(OBJ_DIR)/%.o, $(DUMP_DIR)/%.objdump, $(OBJS))
