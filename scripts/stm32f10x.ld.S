ENTRY(vector_table)

MEMORY
{
  FLASH (rx)      : ORIGIN = FLASH_BASE, LENGTH = FLASH_LEN
  RAM (rwx)       : ORIGIN = RAM_BASE, LENGTH = RAM_LEN
}
REGION_ALIAS("RO", FLASH);
REGION_ALIAS("RW", RAM);

SECTIONS
{
  .text : {
    _stext = .;
    *(.vector_table)
    *(.vector_table*)
    _smaintext = .;
    *(.text)
    *(.text*)
    _emaintext = .;
    *(.rodata)
    *(.rodata*)
    KEEP (*(.init))
    KEEP (*(.fini))
    . = ALIGN(4);
    _etext = .;
  } >RO

  .data : AT (_etext) {
    . = ALIGN(4);
    _sdat = .;
    *(.data)
    *(.data*)
    . = ALIGN(4);
    _edat = .;
    _ldat = LOADADDR(.data);
  } >RW

  .bss : {
    . = ALIGN(8);
    _irq_stackbottom = .;
    . = . + 512;
    _irq_stacktop = .;
    _thread_stackbottom = .;
    . = . + 1024;
    _thread_stacktop = .;
    _sbss = .;
    *(.bss)
    *(.bss*)
    . = ALIGN(4);
    _ebss = .;
  } >RW

  /DISCARD/ : {
    *(.eh_frame)
  }

  .ARM.attributes 0 : { *(.ARM.attributes) }
}
