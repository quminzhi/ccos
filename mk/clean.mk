.PHONY: clean clean-kernel distclean

clean: clean-kernel

clean-kernel:
	@echo "  CLEAN kernel artifacts"
	rm -rf $(OBJ_DIR) $(DUMP_DIR) $(OUT_DIR) \
	       $(TARGET_DISASM) $(TARGET_SYMS)

distclean: clean-kernel
	@echo "  DISTCLEAN kernel artifacts"
	rm -rf $(TMP_DIR)
