/*
 * rp2350.ld.S
 *
 * RP2350 run-from-RAM layout: a small boot section (PICOBIN IMAGE_DEF +
 * copy stub) executes from flash; everything else is linked at SRAM
 * addresses and copied there at boot. The vector table sits at the SRAM
 * base, which satisfies the M33's vector-table alignment requirement.
 */

ENTRY(_rp2350_boot)

MEMORY
{
  FLASH (rx)      : ORIGIN = FLASH_BASE, LENGTH = FLASH_LEN
  RAM (rwx)       : ORIGIN = RAM_BASE, LENGTH = RAM_LEN
}

SECTIONS
{
  .boot : {
    KEEP (*(.rp2350_boot))
    . = ALIGN(4);
  } >FLASH

  .text : AT (LOADADDR(.boot) + SIZEOF(.boot)) {
    _ram_image_start = .;
    _stext = .;
    KEEP (*(.vector_table))
    KEEP (*(.vector_table*))
    _smaintext = .;
    *(.text)
    *(.text*)
    _emaintext = .;
    *(.rodata)
    *(.rodata*)
    KEEP (*(.init))
    KEEP (*(.fini))
    . = ALIGN(4);
    *(.data)
    *(.data*)
    *(.ramfuncs)
    . = ALIGN(4);
    _etext = .;
    _ram_image_end = .;
  } >RAM

  _ram_image_lma = LOADADDR(.text);

  /* The boot stub copies the entire image (text+rodata+data) into SRAM,
   * so there is no separate .data load: make the runtime copy a no-op. */
  _sdat = _etext;
  _edat = _etext;
  _ldat = _etext;

  .bss : {
    . = ALIGN(8);
    _irq_stackbottom = .;
    . = . + 512;
    _irq_stacktop = .;
    _thread_stackbottom = .;
    /* Larger than the STM32 targets' 1kB: SRAM is not scarce here. */
    . = . + 4096;
    _thread_stacktop = .;
    _sbss = .;
    *(.bss)
    *(.bss*)
    . = ALIGN(4);
    _ebss = .;
  } >RAM

  /* Reset flag lives in scratch-Y SRAM, outside the copied/zeroed image,
   * so it survives a warm (watchdog) reset. */
  .flags 0x20081ffc (NOLOAD) : {
    _reset_flag = .;
    . = . + 4;
  }

  /DISCARD/ : {
    *(.eh_frame)
  }

  .ARM.attributes 0 : { *(.ARM.attributes) }
}
