/* AERIA OS: one flat image at 0x40000400 (where the bootstrap unpacks the OS) */
ENTRY(_start)
SECTIONS
{
    . = 0x400df000;
    .text : { *(.text.entry) *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    . = 0x47000000;
    __bss_start = .;
    .bss (NOLOAD) : { *(.bss*) *(COMMON) }
    . = ALIGN(4);
    __bss_end = .;
    /DISCARD/ : { *(.comment) *(.eh_frame) }
}
