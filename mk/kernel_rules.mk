.PHONY: all build disasm-all objdump-objs symbols size debug-sources

all: build

DEBUG_ARTIFACTS :=
ifneq ($(RELEASE),YES)
  DEBUG_ARTIFACTS := disasm-all symbols objdump-objs
endif

build: $(TMP_DIR) $(TARGET) $(TARGET_BIN) $(DEBUG_ARTIFACTS) size

$(TMP_DIR):
	@mkdir -p $@

$(TARGET): $(OBJS)
	@mkdir -p $(dir $@)
	@echo "  LD    $@"
	$(LD) $(CFLAGS) $(OBJS) -o $@ $(LDFLAGS)
ifeq ($(RELEASE),YES)
	@echo "  STRIP $@"
	$(STRIP) --strip-all $@
endif

$(TARGET_BIN): $(TARGET)
	@echo "  OBJCOPY  $@"
	@mkdir -p $(dir $@)
	$(OBJCOPY) -O binary $< $@

$(OUT_DIR) $(OBJ_DIR) $(DUMP_DIR):
	@mkdir -p $@

$(OBJ_DIR)/%.o: %.c | $(TMP_DIR)
	@mkdir -p $(dir $@)
	@echo "  CC    $<"
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/%.o: %.S | $(TMP_DIR)
	@mkdir -p $(dir $@)
	@echo "  AS    $<"
	$(CC) $(CFLAGS) -c $< -o $@

-include $(DEPS)

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

objdump-objs: $(OBJ_DUMPS)

$(DUMP_DIR)/%.objdump: $(OBJ_DIR)/%.o
	@mkdir -p $(dir $@)
	@echo "  OBJDUMP $< -> $@"
	$(OBJDUMP) -d -S $< > $@

debug-sources:
	@echo "ARCH_SRCS     = $(ARCH_SRCS)"
	@echo "PLATFORM_SRCS = $(PLATFORM_SRCS)"
	@echo "KERNEL_SRCS   = $(KERNEL_SRCS)"
	@echo "LIB_SRCS      = $(LIB_SRCS)"
	@echo "SRCS          = $(SRCS)"
	@echo "OBJS          = $(OBJS)"
