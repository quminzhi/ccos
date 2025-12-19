# OpenSBI integration.

.PHONY: opensbi opensbi-warm-offset docker-opensbi docker-opensbi-image docker-opensbi-image-rebuild

OPENSBI_DIR       := opensbi
OPENSBI_BUILD_DIR := $(BUILD_DIR)/opensbi
OPENSBI_PLATFORM  := generic

OPENSBI_FW_JUMP     := $(OPENSBI_BUILD_DIR)/platform/$(OPENSBI_PLATFORM)/firmware/fw_jump.elf
OPENSBI_FW_JUMP_BIN := $(OPENSBI_BUILD_DIR)/platform/$(OPENSBI_PLATFORM)/firmware/fw_jump.bin

# ISA: 板级 DTS 为 rv64imafd（无 C 扩展），必须禁用 C。
OPENSBI_PLATFORM_RISCV_ISA ?= rv64imafd_zicsr_zifencei

# Keep in sync with FSBL's staging addresses.
OPENSBI_FW_TEXT_START    ?= 0x80000000
OPENSBI_FW_JUMP_ADDR     ?= 0x80200000
OPENSBI_FW_JUMP_FDT_ADDR ?= 0x88000000

opensbi: $(TMP_DIR) $(OPENSBI_FW_JUMP)

opensbi-warm-offset: $(OPENSBI_FW_JUMP)
	@python3 $(PROJECT_ROOT)/scripts/opensbi_warm_offset.py \
		--nm $(OPENSBI_CROSS_COMPILE)nm \
		--elf $(OPENSBI_FW_JUMP) \
		--base $(OPENSBI_FW_TEXT_START)

$(OPENSBI_FW_JUMP):
	@echo "  OPENSBI PLATFORM=$(OPENSBI_PLATFORM) FW_JUMP=y"
	@echo "         FW_TEXT_START=$(OPENSBI_FW_TEXT_START) FW_JUMP_ADDR=$(OPENSBI_FW_JUMP_ADDR) FW_JUMP_FDT_ADDR=$(OPENSBI_FW_JUMP_FDT_ADDR)"
	@mkdir -p $(OPENSBI_BUILD_DIR)
	$(MAKE) -C $(OPENSBI_DIR) \
		PLATFORM=$(OPENSBI_PLATFORM) \
		PLATFORM_RISCV_ISA=$(OPENSBI_PLATFORM_RISCV_ISA) \
		FW_JUMP=y \
		FW_TEXT_START=$(OPENSBI_FW_TEXT_START) \
		FW_JUMP_ADDR=$(OPENSBI_FW_JUMP_ADDR) \
		FW_JUMP_FDT_ADDR=$(OPENSBI_FW_JUMP_FDT_ADDR) \
		CROSS_COMPILE=$(OPENSBI_CROSS_COMPILE) \
		O=$(abspath $(OPENSBI_BUILD_DIR))

$(OPENSBI_FW_JUMP_BIN): $(OPENSBI_FW_JUMP)

OPENSBI_DOCKER_CROSS_COMPILE ?= riscv64-linux-gnu-
OPENSBI_DOCKER_IMAGE ?= opensbi-build:linux-gnu
OPENSBI_DOCKER_WS    ?= /ws
PROJECT_ROOT := $(abspath .)
OPENSBI_DOCKERFILE := $(PROJECT_ROOT)/Dockerfile.opensbi

docker-opensbi: docker-opensbi-image
	@echo "  OPENSBI (docker) PLATFORM=$(OPENSBI_PLATFORM) O=$(OPENSBI_BUILD_DIR)"
	@mkdir -p $(OPENSBI_BUILD_DIR)
	@docker run --rm -t \
		-u "$$(id -u):$$(id -g)" \
		-v "$(PROJECT_ROOT):$(OPENSBI_DOCKER_WS)" \
		-w "$(OPENSBI_DOCKER_WS)" \
		"$(OPENSBI_DOCKER_IMAGE)" \
		bash -lc 'make -C "$(OPENSBI_DIR)" \
			PLATFORM="$(OPENSBI_PLATFORM)" \
			FW_JUMP=y \
			FW_TEXT_START="$(OPENSBI_FW_TEXT_START)" \
			FW_JUMP_ADDR="$(OPENSBI_FW_JUMP_ADDR)" \
			FW_JUMP_FDT_ADDR="$(OPENSBI_FW_JUMP_FDT_ADDR)" \
			CROSS_COMPILE="$(OPENSBI_DOCKER_CROSS_COMPILE)" \
			O="$(OPENSBI_DOCKER_WS)/$(OPENSBI_BUILD_DIR)"'

docker-opensbi-image:
	@docker image inspect "$(OPENSBI_DOCKER_IMAGE)" >/dev/null 2>&1 || ( \
		echo "  DOCKER BUILD $(OPENSBI_DOCKER_IMAGE)"; \
		docker build -t "$(OPENSBI_DOCKER_IMAGE)" -f "$(OPENSBI_DOCKERFILE)" "$(PROJECT_ROOT)"; \
	)

docker-opensbi-image-rebuild:
	@echo "  DOCKER REBUILD $(OPENSBI_DOCKER_IMAGE)"
	@docker build -t "$(OPENSBI_DOCKER_IMAGE)" -f "$(OPENSBI_DOCKERFILE)" "$(PROJECT_ROOT)"
