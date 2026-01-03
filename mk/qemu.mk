# QEMU helper targets.

.PHONY: qemu debug qemu-dbg gdb

# OpenSBI fw_jump (built out-of-tree under out/opensbi/<board> by default).
OPENSBI_PLATFORM ?= generic
OPENSBI_OUT ?= $(call out_dir,opensbi,$(BOARD))
OPENSBI_FW_JUMP_BIN ?= $(OPENSBI_OUT)/platform/$(OPENSBI_PLATFORM)/firmware/fw_jump.bin
OPENSBI_FW_JUMP_ADDR ?= 0x80200000

QEMU_OPTS = -machine $(QEMU_MACHINE)$(QEMU_MACHINE_EXTRAS) \
            $(QEMU_SMP_OPTS) $(QEMU_COMMON_OPTS)

DTB := $(OUT_DIR)/virt.dtb
DTS := $(OUT_DIR)/virt.dts

qemu: $(TARGET_BIN) $(DTB) $(DTS) $(OPENSBI_FW_JUMP_BIN)
	@echo "  QEMU  $(TARGET) (OpenSBI: $(OPENSBI_FW_JUMP_BIN))"
	$(QEMU) $(QEMU_OPTS) \
		-bios $(OPENSBI_FW_JUMP_BIN) \
		-dtb $(DTB) \
		-device loader,file=$(TARGET_BIN),addr=$(OPENSBI_FW_JUMP_ADDR)

debug: qemu-dbg

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
	@mkdir -p $(OUT_DIR)
	$(QEMU) \
		-machine $(QEMU_MACHINE)$(QEMU_MACHINE_EXTRAS),dumpdtb=$@ \
		$(QEMU_SMP_OPTS) $(QEMU_COMMON_OPTS) -S

$(OPENSBI_FW_JUMP_BIN):
	$(MAKE) -f $(REPO_ROOT)/firmware/opensbi/integration/opensbi.mk opensbi \
		REPO_ROOT=$(REPO_ROOT) OUT=$(OPENSBI_OUT) \
		OPENSBI_PLATFORM=$(OPENSBI_PLATFORM) OPENSBI_CROSS_COMPILE=$(OPENSBI_CROSS_COMPILE)
