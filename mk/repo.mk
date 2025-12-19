# Repo root detection for CCOS helper targets (e.g., QEMU/OpenSBI integration).
#
# - When invoked from the top-level repo, the root Makefile passes REPO_ROOT=...
# - When invoked directly under kernels/ccos, derive REPO_ROOT by stripping the
#   known suffix from this file path (no parent-directory segments).

CCOS_MK_DIR := $(patsubst %/,%,$(abspath $(dir $(lastword $(MAKEFILE_LIST)))))
REPO_ROOT ?= $(patsubst %/kernels/ccos/mk,%,$(CCOS_MK_DIR))
