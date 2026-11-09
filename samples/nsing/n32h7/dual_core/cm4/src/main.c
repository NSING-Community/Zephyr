/*
 * Copyright (c) 2026 Nations Technologies
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>

int main(void)
{
	uint32_t seconds = 0U;

	printk("Hello from the Cortex-M4 image\n");

	while (1) {
		k_msleep(1000);
		printk("Cortex-M4 alive for %u s\n", ++seconds);
	}

	return 0;
}
