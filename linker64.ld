/* Linker script for the 64-bit kernel (loaded at 2MB) */
ENTRY(long_mode_entry)

SECTIONS
{
    . = 0x200000;

    .text ALIGN(16) :
    {
        *(.text.entry)
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

    _bss_start = .;
    .bss ALIGN(16) :
    {
        *(COMMON)
        *(.bss)
        *(.bss.*)
    }
    _bss_end = .;

    _kernel_end = .;

    /DISCARD/ :
    {
        *(.comment)
        *(.note.*)
        *(.eh_frame*)
    }
}
