/* Linker script for the 32-bit multiboot boot stub */
ENTRY(_start)

SECTIONS
{
    . = 1M;

    .multiboot ALIGN(4) :
    {
        KEEP(*(.multiboot))
    }

    .text ALIGN(16) :
    {
        *(.text)
        *(.text.*)
    }

    .rodata ALIGN(16) :
    {
        *(.rodata)
        *(.rodata.*)
    }

    .data ALIGN(16) :
    {
        *(.data)
        *(.data.*)
    }

    .bss ALIGN(4096) :
    {
        *(.bss)
        *(.bss.*)
    }

    /* Embed 64-bit kernel at 2MB */
    . = 0x200000;
    .kernel64 ALIGN(16) :
    {
        *(.kernel64)
    }

    /DISCARD/ :
    {
        *(.comment)
        *(.note.*)
        *(.eh_frame*)
    }
}
