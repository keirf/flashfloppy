ENTRY(vector_table)

#if 0
MEMORY
{
  FLASH (rx)      : ORIGIN = 0x08000000, LENGTH = 128K
  RAM (rwx)       : ORIGIN = 0x20000000, LENGTH = 64K
}
#else
MEMORY
{
#define FLASH RAM
  RAM (rwx)       : ORIGIN = 0x20004000, LENGTH = 48K
}
#endif

SECTIONS
{
  .text : {
    _stext = .;
    *(.vector_table)
    *(.text)
    *(.text*)
    *(.rodata)
    *(.rodata*)
    KEEP (*(.init))
    KEEP (*(.fini))
    . = ALIGN(4);
    _etext = .;
  } >FLASH

  .data ALIGN(4) : {
    _sdat = .;
    *(.data)
    *(.data*)
    . = ALIGN(4);
    _edat = .;
  } >RAM

  .bss ALIGN(4) : {
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
