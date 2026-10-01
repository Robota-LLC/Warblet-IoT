/* STM32WBA65RI memory layout: 2 MB flash and 448 KB SRAM1.
 * Reserve the last 8 KB flash page for config; leave SRAM2 unused. */

ENTRY(Reset_Handler)

MEMORY
{
  FLASH  (rx)  : ORIGIN = 0x08000000, LENGTH = 2040K   /* 2 MB - 8 KB config page */
  CONFIG (r)   : ORIGIN = 0x081FE000, LENGTH = 8K
  RAM    (rwx) : ORIGIN = 0x20000000, LENGTH = 448K    /* SRAM1 */
}

_estack    = ORIGIN(RAM) + LENGTH(RAM);
_stack_size = 8K;
_heap_size  = 0;

SECTIONS
{
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector))
    . = ALIGN(4);
  } > FLASH

  .text :
  {
    . = ALIGN(4);
    *(.text) *(.text*)
    *(.glue_7) *(.glue_7t)
    *(.eh_frame)
    KEEP(*(.init))
    KEEP(*(.fini))
    . = ALIGN(4);
    _etext = .;
  } > FLASH

  .rodata :
  {
    . = ALIGN(4);
    *(.rodata) *(.rodata*)
    . = ALIGN(4);
  } > FLASH

  .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } > FLASH
  .ARM       : { __exidx_start = .; *(.ARM.exidx*) __exidx_end = .; } > FLASH

  .preinit_array : { PROVIDE_HIDDEN(__preinit_array_start = .); KEEP(*(.preinit_array*)) PROVIDE_HIDDEN(__preinit_array_end = .); } > FLASH
  .init_array    : { PROVIDE_HIDDEN(__init_array_start = .);    KEEP(*(SORT(.init_array.*))) KEEP(*(.init_array*)) PROVIDE_HIDDEN(__init_array_end = .); } > FLASH
  .fini_array    : { PROVIDE_HIDDEN(__fini_array_start = .);    KEEP(*(SORT(.fini_array.*))) KEEP(*(.fini_array*)) PROVIDE_HIDDEN(__fini_array_end = .); } > FLASH

  _sidata = LOADADDR(.data);

  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data) *(.data*)
    . = ALIGN(4);
    _edata = .;
  } > RAM AT> FLASH

  .bss :
  {
    . = ALIGN(4);
    _sbss = .;
    __bss_start__ = _sbss;
    *(.bss) *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
    __bss_end__ = _ebss;
  } > RAM

  ._user_heap_stack :
  {
    . = ALIGN(8);
    . = . + _heap_size;
    . = . + _stack_size;
    . = ALIGN(8);
  } > RAM

  /DISCARD/ : { libc.a(*) libm.a(*) libgcc.a(*) }

  .ARM.attributes 0 : { *(.ARM.attributes) }
}
