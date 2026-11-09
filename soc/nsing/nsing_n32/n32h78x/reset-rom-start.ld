/* SPDX-License-Identifier: Apache-2.0 */

/* N32H7 boot ROM expects the Cortex-M reset stub in the first Flash window. */
KEEP(*(.text._reset_section))

#ifdef CONFIG_SOC_N32H787_CM4
/* The Cortex-M MPU helper uses the generic RAM relocation symbol names.
 * Cortex-M4 MPU regions require a power-of-two size aligned to that size,
 * and SRAMCODE starts at 0x30000400, so cover the relocated text with a
 * 64-KiB RX window at the base of AHB SRAM1.  The window ends below sram0. */
PROVIDE(__ram_text_reloc_start = ABSOLUTE(0x30000000));
PROVIDE(__ram_text_reloc_size = ABSOLUTE(0x10000));
ASSERT(__sramcode_text_reloc_size <= 0xFC00,
       "N32H7 CM4 SRAM text exceeds its 64-KiB MPU window");
#endif
