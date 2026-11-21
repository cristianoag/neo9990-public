/* SPDX-License-Identifier: GPL-2.0-only */
ENTRY(_start)
SECTIONS
{
    . = 0x42000000;
    .text : { KEEP(*(.text.boot)) *(.text*) }
    .vectors : ALIGN(2048) { KEEP(*(.vectors)) }
    .rodata : ALIGN(16) { *(.rodata*) }
    .data : ALIGN(16) { *(.data*) }
    .bss (NOLOAD) : ALIGN(16) {
        __bss_start = .;
        *(.bss*) *(COMMON)
        . = ALIGN(16);
        __bss_end = .;
    }
    .stack (NOLOAD) : ALIGN(16) {
        . += 0x10000;
        __stack_top = .;
    }
    __image_end = .;
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
ASSERT(__image_end < 0x43000000, "Firmware exceeds reserved 16 MiB window")
