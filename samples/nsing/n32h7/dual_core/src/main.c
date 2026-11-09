/*
 * Copyright (c) 2026 Nations Technologies
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>

int main(void)
{
	printk("Hello from the Cortex-M7 image\n");
	printk("Releasing the Cortex-M4 image\n");

	while (1) {
		k_msleep(1000);
	}

	return 0;
}
