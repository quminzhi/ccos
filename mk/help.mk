# Help target.

.PHONY: help

help:
	@echo "ccos Makefile targets:"
	@echo "  build         Build kernel (ELF/bin) + disasm/symbols/objdumps/size"
	@echo "  qemu          Run QEMU (fw_jump + kernel.bin + dtb)"
	@echo "  qemu-dbg      Run QEMU paused with GDB stub"
	@echo "  gdb           Attach GDB to qemu-dbg (port $(QEMU_GDB_PORT))"
	@echo "  clean         Remove kernel build artifacts"
	@echo "  distclean     Clean kernel artifacts"
	@echo ""
	@echo "Common variables:"
	@echo "  CPUS=<n>      Number of harts (default: $(CPUS))"
	@echo "  RELEASE=YES   Enable release flags"
	@echo "  QEMU_MEM=256M QEMU memory size"
	@echo "  CROSS_COMPILE=<prefix>        Kernel toolchain prefix"
