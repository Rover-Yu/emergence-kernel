/* Linker script for AP trampoline (16-bit) */
ENTRY(trampoline_start)
SECTIONS
{
    /* Start at offset 8 to account for objcopy header
     * objcopy adds 8 bytes: 2 bytes padding + 4 bytes symbol + 2 bytes padding */
    . = 8;
    .text : {
        *(.text)
    }
}
