# ccos build entrypoint (intentionally thin).
#
# This Makefile only wires together the modular make fragments under mk/.
# The included files define variables, targets, and default goals.

# ---- Core repo/board settings ----
include mk/repo.mk          # repo root detection, board selection
include mk/config.mk        # config fragments and feature toggles

# ---- Toolchain and paths ----
include mk/toolchain.mk     # CROSS_COMPILE, CC/LD/AS, etc.
include mk/paths.mk         # output directories and source paths
include mk/flags.mk         # CFLAGS/ASFLAGS/LDFLAGS defaults

# ---- Build rules and helpers ----
include mk/kernel_rules.mk  # compile/link rules + kernel image targets
include mk/qemu.mk          # QEMU run/debug targets
include mk/clean.mk         # clean/distclean targets
include mk/help.mk          # help + default goal
