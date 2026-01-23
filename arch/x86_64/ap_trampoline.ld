/* Linker script for AP trampoline (16-bit) */
ENTRY(trampoline_start)
SECTIONS
{
    /* Start at offset 0 - AP boots at physical 0x7000 via STARTUP IPI
     * No offset needed - objcopy creates raw binary without headers */
    . = 0;
    .text : {
        *(.text)
    }
}
