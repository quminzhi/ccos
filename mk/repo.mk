# Repo root detection (shared helper).
#
# - Centralizes REPO_ROOT/BOARD/CROSS_COMPILE defaults via mk/common.mk.
# - Callers can set DEFAULT_* before including this file.

CCOS_MK_DIR := $(patsubst %/,%,$(abspath $(dir $(lastword $(MAKEFILE_LIST)))))
COMMON_MK := $(CCOS_MK_DIR)/../../../mk/common.mk
include $(COMMON_MK)
include $(REPO_ROOT)/mk/build.mk
$(eval $(call apply_defaults))
