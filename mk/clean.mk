.PHONY: clean clean-kernel clean-opensbi distclean

clean: clean-kernel

clean-kernel:
	@echo "  CLEAN kernel artifacts"
	rm -rf $(OBJ_DIR) $(DUMP_DIR) $(OUT_DIR) \
	       $(TARGET_DISASM) $(TARGET_SYMS)

clean-opensbi:
	@echo "  CLEAN OpenSBI artifacts"
	$(MAKE) -C $(OPENSBI_DIR) \
		O=$(abspath $(OPENSBI_BUILD_DIR)) \
		clean || true

distclean: clean-kernel
	@echo "  DISTCLEAN kernel + OpenSBI"
	rm -rf $(OPENSBI_BUILD_DIR)

