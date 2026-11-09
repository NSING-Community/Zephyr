/*
 * Copyright (c) 2026 Nations Technologies
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/init.h>
#include <zephyr/linker/sections.h>
#include <zephyr/cache.h>
#include <zephyr/devicetree.h>
#include <soc.h>
#include "n32h7_hal.h"
#include <n32h7_shared.h>

extern uint32_t SystemCoreClock;

/*
 * Kept outside .bss so a debugger can identify the last completed boot
 * phase even if the application never reaches main().
 */
Z_GENERIC_SECTION(.noinit) volatile uint32_t n32h78x_boot_stage;
Z_GENERIC_SECTION(.noinit) volatile uint32_t n32h78x_wake_count;
Z_GENERIC_SECTION(.noinit) volatile uint32_t n32h78x_reset_cause;
Z_GENERIC_SECTION(.noinit) volatile uint32_t n32h78x_reset_count;
Z_GENERIC_SECTION(.noinit) volatile uint32_t n32h78x_reset_magic;

#define N32H7_RESET_MAGIC 0x4E525354U

#ifdef CONFIG_SOC_N32H787_CM4
extern char _vector_start[];
extern char _vector_end[];

/*
 * z_prep_c() calls the generic weak relocate_vector_table() after
 * soc_reset_hook().  The generic Cortex-M implementation points VTOR back
 * at _vector_start (the CM4 xSPI window), which defeats the early copy below
 * and makes IRQ entry depend on the independent xSPI mapping.  Keep the
 * final VTOR value in the CM4 vector SRAM window instead.
 */
void relocate_vector_table(void)
{
	uintptr_t vector_addr = DT_REG_ADDR(DT_NODELABEL(cm4_vector_ram));

	*(volatile uint32_t *)0xE000ED08UL = vector_addr;
	__asm__ volatile("dsb" ::: "memory");
	__asm__ volatile("isb" ::: "memory");
}

static void n32h7_cm4_relocate_vectors(void)
{
	/* The SDK places the CM4 vector table in AHB SRAM2, not in the
	 * CM4 application SRAM/stack bank.  Keep the address tied to DTS so
	 * the memory map and this early boot operation cannot silently diverge.
	 */
	volatile uint32_t *dst = (volatile uint32_t *)DT_REG_ADDR(
		DT_NODELABEL(cm4_vector_ram));
	const volatile uint32_t *src = (const volatile uint32_t *)N32H78X_CM4_FLASH_BASE;
	uintptr_t vector_size = (uintptr_t)_vector_end - (uintptr_t)_vector_start;
	uintptr_t vector_ram_size = DT_REG_SIZE(DT_NODELABEL(cm4_vector_ram));

	n32h78x_boot_stage = 0x4E333210U; /* before CM4 vector copy */
	if (vector_size == 0U || vector_size > vector_ram_size) {
		/* Do not release CM4 with a truncated/out-of-bounds vector table. */
		n32h78x_boot_stage = 0x4E33321EU;
		for (;;) {
			__asm__ volatile("wfi" ::: "memory");
		}
	}
	/* Copy the complete linker-defined vector table. */
	vector_size = (vector_size + sizeof(uint32_t) - 1U) & ~(sizeof(uint32_t) - 1U);
	for (uintptr_t i = 0U; i < vector_size / sizeof(uint32_t); i++) {
		dst[i] = src[i];
	}
	(void)sys_cache_data_flush_range((void *)dst, vector_size);
	__asm__ volatile("dsb" ::: "memory");
	*(volatile uint32_t *)0xE000ED08UL = (uintptr_t)dst;
	__asm__ volatile("dsb" ::: "memory");
	__asm__ volatile("isb" ::: "memory");
	n32h78x_boot_stage = 0x4E333214U; /* CM4 VTOR programmed */
}
#endif

#ifdef CONFIG_SOC_RESET_HOOK
/*
 * N32H7's CM7 reset sequence requires the vendor system initialization:
 * it enables the FPU, selects the CM7 Flash vector window, configures TCM,
 * and selects the board's SMPS supply.  The SDK startup calls this before
 * entering main; run the same hook before Zephyr initializes its MPU/RTOS.
 */
void soc_reset_hook(void)
{
	if (n32h78x_reset_magic != N32H7_RESET_MAGIC) {
		n32h78x_reset_magic = N32H7_RESET_MAGIC;
		n32h78x_reset_count = 0U;
	}
	n32h78x_reset_count++;
	n32h78x_reset_cause = RCC->CTRLSTS;
	n32h78x_boot_stage = 0x4E333201U; /* reset hook entered */
	SystemInit();
	/* The reset image deliberately remains on HSI (64 MHz).  The vendor
	 * startup file carries a board-independent default which may describe a
	 * PLL configuration that is not enabled by this Zephyr port.
	 */
	SystemCoreClock = 64000000U;
#ifndef CONFIG_SOC_N32H787_CM4
	/* CM7 owns the shared control block and establishes the boot generation
	 * that CM4 must observe before touching shared peripherals.
	 */
	n32h7_shared_reset_for_cm7();
	n32h7_shared_publish_cm7_reset_cause(n32h78x_reset_cause);
#else
	/* Preserve the CM4-originated reset cause for the next CM7 boot. */
	n32h7_shared_publish_cm4_reset_cause(n32h78x_reset_cause);
#endif
	n32h78x_boot_stage = 0x4E333202U; /* vendor system init complete */
#ifdef CONFIG_SOC_N32H787_CM4
	/* Must run before z_prep_c(): soc_early_init_hook() is reached only from
	 * z_cstart(), after the early vector/interrupt setup has already run.
	 */
	n32h7_cm4_relocate_vectors();
#endif
}
#endif

void soc_early_init_hook(void)
{
	n32h78x_boot_stage = 0x4E333210U; /* early hook entered */
	/* Match the vendor RCC_ClearResetFlag() sequence: pulse RMRSTF and
	 * return it to zero so subsequent reset causes can latch normally.
	 */
	RCC->CTRLSTS |= RCC_CTRLSTS_RMRSTF;
	RCC->CTRLSTS &= ~RCC_CTRLSTS_RMRSTF;
	n32h78x_boot_stage = 0x4E333211U; /* reset flag clear complete */
	#ifdef CONFIG_N32H78X_DCMU_INIT
	#ifndef CONFIG_SOC_N32H787_CM4
	n32h7_dcmu_reset();
	#endif
	n32h7_dcmu_enable();
	#endif
	#ifdef CONFIG_SOC_N32H787_CM4
	#ifdef CONFIG_N32H78X_CM4_WAIT_FOR_CM7
	/* Match STM32H7's CM4 HSEM boot gate.  CM7 publishes this state before
	 * releasing CM4, so CM4 drivers cannot race global RCC setup.
	 */
	uint32_t wait_loops = 0U;

	while (!n32h7_shared_cm7_is_ready() &&
	       wait_loops++ < CONFIG_N32H78X_CM4_BOOT_TIMEOUT_LOOPS) {
		/* The DCMU interrupt is initialized later; do not use WFE here
		 * because a standalone shared-memory publish has no SEV source.
		 */
		__asm__ volatile("nop" ::: "memory");
	}
	if (!n32h7_shared_cm7_is_ready()) {
		n32h7_shared_mark_cm4_error();
		for (;;) {
			__asm__ volatile("wfi" ::: "memory");
		}
	}
	n32h7_shared_mark_cm4_running();
	#endif
	#endif
	/* Keep debug clocks running only for an explicitly requested low-power
	 * test. Production images leave these bits clear for representative
	 * current measurements.
	 */
	#ifdef CONFIG_N32H78X_DEBUG_LOW_POWER
	DBG->CTRL |= DBG_CTRL_M7SLEEP | DBG_CTRL_M7STOP | DBG_CTRL_M7STBY |
		DBG_CTRL_M4SLEEP | DBG_CTRL_M4STOP | DBG_CTRL_M4STBY;
	#endif
	/* The Zephyr vector table is linked at the XSPI1 remap address. */
	#ifdef CONFIG_SOC_N32H787_CM4
	/* Vector relocation is performed in soc_reset_hook before z_prep_c. */
	#else
	*(volatile uint32_t *)0xE000ED08UL = N32H78X_FLASH_BASE;
	#endif
	__asm__ volatile("dsb" ::: "memory");
	__asm__ volatile("isb" ::: "memory");
	n32h78x_boot_stage = 0x4E333203U; /* Zephyr early init complete */
}

#ifdef CONFIG_N32H78X_START_M4
/* Release the independently linked CM4 image after kernel drivers exist. */
static int n32h78x_start_m4(void)
{
	/* Publish CM7 readiness before releasing CM4, matching the safety
	 * library's READY phase without touching the not-yet-validated DCMU.
	 */
	n32h7_shared_publish_cm7_ready();
	__asm__ volatile("dsb" ::: "memory");
	n32h7_cm4_release(N32H78X_CM4_FLASH_BASE);
	n32h78x_boot_stage = 0x4E333204U; /* CM4 reset released */
	return 0;
}

/* Match STM32H7's dual-core sequencing: release CM4 after application-level
 * CM7 initialization, rather than at the earliest POST_KERNEL priority.
 */
SYS_INIT(n32h78x_start_m4, POST_KERNEL, CONFIG_APPLICATION_INIT_PRIORITY);
#endif

#ifdef CONFIG_N32H78X_DISABLE_WFI
bool z_arm_on_enter_cpu_idle(void)
{
	return false;
}
#endif
