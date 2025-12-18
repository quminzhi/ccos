# Global configuration knobs.

# Debug / Release 模式：make RELEASE=YES 切到 Release
RELEASE ?= NO

# CPU：make CPUS=4
CPUS ?= 2

# 目标配置
ARCH     := riscv
BOARD    := virt
PLATFORM := qemu-virt-sbi

# QEMU 运行配置
QEMU_MACHINE        ?= virt
QEMU_MACHINE_EXTRAS ?=
QEMU_MEM            ?= 256M
QEMU_COMMON_OPTS    ?= -nographic -m $(QEMU_MEM)
QEMU_SMP_OPTS       ?= -smp $(CPUS)

QEMU          ?= qemu-system-riscv64
QEMU_GDB_PORT ?= 1234

