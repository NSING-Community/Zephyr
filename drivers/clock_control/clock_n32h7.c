/*
 * Copyright (c) 2026 Nations Technologies
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT nsing_n32h7_rcc

#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/sys/util.h>
#include <n32h7_hal.h>
#include <n32h7xx_rcc.h>

#define N32H7_GATE_SLOTS 128U

static uint8_t gate_ref[N32H7_GATE_SLOTS * 32U];

static int n32h7_gate(clock_control_subsys_t sys, bool enable)
{
	uintptr_t id = (uintptr_t)sys;
	uint32_t offset = (uint32_t)(id >> 6);
	uint32_t bit = (uint32_t)(id & 0x1FU);
	volatile uint32_t *reg;
	uint32_t slot;

	/* RCC clock gates are memory-mapped beyond the end of some CMSIS
	 * register-struct revisions; validate the encoded bus offset itself.
	 */
	if ((offset & 3U) != 0U || offset > 0x1FCU) {
		return -EINVAL;
	}

	slot = (offset >> 2) * 32U + bit;
	reg = (volatile uint32_t *)((uintptr_t)RCC + offset);

	if (enable) {
		if (gate_ref[slot]++ != 0U) {
			return 0;
		}
	} else {
		if (gate_ref[slot] == 0U) {
			return 0;
		}
		if (--gate_ref[slot] != 0U) {
			return 0;
		}
	}

	if (enable) {
		*reg |= BIT(bit);
	} else {
		*reg &= ~BIT(bit);
	}

	return 0;
}

static int n32h7_clock_on(const struct device *dev, clock_control_subsys_t sys)
{
	ARG_UNUSED(dev);

	return n32h7_gate(sys, true);
}

static int n32h7_clock_off(const struct device *dev, clock_control_subsys_t sys)
{
	ARG_UNUSED(dev);

	return n32h7_gate(sys, false);
}

static int n32h7_clock_rate(const struct device *dev, clock_control_subsys_t sys, uint32_t *rate)
{
	uintptr_t id = (uintptr_t)sys;
	uint32_t offset = (uint32_t)(id >> 6);
	RCC_ClocksTypeDef clocks;

	ARG_UNUSED(dev);

	RCC_GetClocksFreqValue(&clocks);

	switch (offset) {
	case N32H7_RCC_AHB1EN1:
	case N32H7_RCC_AHB1EN2:
	case N32H7_RCC_AHB1EN3:
	case N32H7_RCC_AHB1EN4:
		*rate = clocks.AHB1ClkFreq;
		break;
	case N32H7_RCC_AHB2EN1:
	case N32H7_RCC_AHB2EN2:
		*rate = clocks.AHB2ClkFreq;
		break;
	case N32H7_RCC_APB1EN1:
	case N32H7_RCC_APB1EN2:
	case N32H7_RCC_APB1EN3:
	case N32H7_RCC_APB1EN4:
	case N32H7_RCC_APB1EN5:
		*rate = clocks.APB1ClkFreq;
		break;
	case N32H7_RCC_APB2EN1:
	case N32H7_RCC_APB2EN2:
	case N32H7_RCC_APB2EN3:
	case N32H7_RCC_APB2EN4:
		*rate = clocks.APB2ClkFreq;
		break;
	case N32H7_RCC_AHB5EN1:
	case N32H7_RCC_AHB5EN2:
		*rate = clocks.AHB5ClkFreq;
		break;
	case N32H7_RCC_APB5EN1:
	case N32H7_RCC_APB5EN2:
		*rate = clocks.APB5ClkFreq;
		break;
	case N32H7_RCC_AXIEN1:
	case N32H7_RCC_AXIEN2:
	case N32H7_RCC_AXIEN3:
	case N32H7_RCC_AXIEN4:
		*rate = clocks.AXIClkFreq;
		break;
	case N32H7_RCC_AHB9EN1:
		*rate = clocks.AHB9ClkFreq;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static DEVICE_API(clock_control, n32h7_clock_api) = {
	.on = n32h7_clock_on,
	.off = n32h7_clock_off,
	.get_rate = n32h7_clock_rate,
};

DEVICE_DT_DEFINE(DT_NODELABEL(rcc), NULL, NULL, NULL, NULL,
		 PRE_KERNEL_1, CONFIG_CLOCK_CONTROL_INIT_PRIORITY, &n32h7_clock_api);
