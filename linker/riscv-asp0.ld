OUTPUT_ARCH(riscv)
ENTRY(_start)

PHDRS
{
  text PT_LOAD FLAGS(5); /* R | X */
  data PT_LOAD FLAGS(6); /* R | W */
}

/*
 * ASP0 + OpenSBI fw_jump
 *
 * DDR base: 0x8000_0000
 * payload (kernel) 常用起点: 0x8020_0000
 * DTB 常用地址:            0x8800_0000
 */
MEMORY
{
  /* Keep kernel below the DTB handoff address (0x8800_0000). */
  RAM (rwx) : ORIGIN = 0x80200000, LENGTH = 0x07E00000 /* 126 MiB */
}

STACK_SIZE = 0x1000;  /* 4KB */

SECTIONS
{
  . = ORIGIN(RAM);

  .text : ALIGN(4)
  {
    KEEP(*(.text.entry))
    *(.text .text.*)
  } > RAM :text

  .rodata : ALIGN(4)
  {
    *(.rodata .rodata.*)
  } > RAM :text

  .data : ALIGN(16)
  {
    __data_start = .;
    *(.data .data.*)
    *(.sdata .sdata.*)
    __data_end = .;
  } > RAM :data

  .bss (NOLOAD) : ALIGN(16)
  {
    __bss_start = .;
    *(.bss .bss.*)
    *(.sbss .sbss.*)
    *(COMMON)
    __bss_end = .;
  } > RAM :data

  /* 程序镜像末尾（bss 后面） */
  __image_end = .;

  /* 栈放在 RAM 顶端（只定义符号，不建输出段） */
  __stack_top    = ORIGIN(RAM) + LENGTH(RAM);
  __stack_bottom = __stack_top - STACK_SIZE;

  /* 关键：保证段布局不会撞到栈 */
  ASSERT(__image_end <= __stack_bottom, "RAM overflow: sections overlap stack")
}

