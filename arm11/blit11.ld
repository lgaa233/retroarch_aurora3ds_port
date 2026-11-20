/* The ARM11 scaler job, run in place at RA_BLIT11_CODE (blit11.h), entry
 * first. Nothing clears memory for it, so it may have no .bss. */
OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(blit11_entry)

SECTIONS
{
    . = 0x23D00000;

    .text :
    {
        KEEP(*(.text.entry))
        *(.text .text.*)
        *(.rodata .rodata.*)
        . = ALIGN(4);
    }

    .data :
    {
        *(.data .data.*)
        . = ALIGN(4);
    }

    .bss :
    {
        *(.bss .bss.*)
        *(COMMON)
    }

    ASSERT(SIZEOF(.data) == 0, "the ARM11 job must not have .data")
    ASSERT(SIZEOF(.bss) == 0, "the ARM11 job must not use .bss: nothing clears it")
    ASSERT(. <= 0x23D10000, "the ARM11 job is larger than RA_BLIT11_CODE_MAX")

    /DISCARD/ :
    {
        *(.ARM.exidx*)
        *(.ARM.extab*)
        *(.ARM.attributes*)
        *(.comment*)
        *(.note*)
    }
}
