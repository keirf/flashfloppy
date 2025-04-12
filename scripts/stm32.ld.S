ENTRY(vector_table)

MEMORY
{
  FLASH (rx)      : ORIGIN = FLASH_BASE, LENGTH = FLASH_LEN
  RAM (rwx)       : ORIGIN = RAM_BASE, LENGTH = RAM_LEN
}

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
  } >FLASH

  .flags : {
    _reset_flag = .;
    . = . + 4;
  } >RAM

  .data : AT (_etext) {
    . = ALIGN(4);
    _sdat = .;
    *(.data)
    *(.data*)
    *(.ramfuncs)
    . = ALIGN(4);
    _edat = .;
    _ldat = LOADADDR(.data);
  } >RAM

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
  } >RAM

  /DISCARD/ : {
    *(.eh_frame)
  }

  .ARM.attributes 0 : { *(.ARM.attributes) }
}
