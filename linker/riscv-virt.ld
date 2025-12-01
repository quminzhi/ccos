OUTPUT_ARCH(riscv)
ENTRY(_start)

/* 定义程序头（Program Headers），控制 LOAD 段的权限 */
PHDRS
{
  text PT_LOAD FLAGS(5); /* R | X = 0b101 */
  data PT_LOAD FLAGS(6); /* R | W = 0b110 */
}

SECTIONS
{
  . = 0x80200000;

  /* 代码 + 只读数据，放到 text 段（RX） */
  .text : ALIGN(4)
  {
    *(.text.entry)
    *(.text .text.*)
  } :text

  .rodata : ALIGN(4)
  {
    *(.rodata .rodata.*)
  } :text

  /* 可写数据，放到 data 段（RW） */
  .data : ALIGN(4)
  {
    *(.data .data.*)
  } :data

  .bss : ALIGN(4)
  {
    __bss_start = .;
    *(.bss .bss.*)
    *(COMMON)
    __bss_end = .;
  } :data
}