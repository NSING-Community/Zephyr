/*
 * Copyright (c) 2026 Nations Technologies
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT nsing_n32h7_gpio

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_utils.h>

#include <soc.h>
#include <n32h7_hal.h>

struct n32h7_gpio_config {
	struct gpio_driver_config common;
	GPIO_Module *regs;
	clock_control_subsys_t clock_subsys;
};

static int n32h7_gpio_pin_configure(const struct device *dev, gpio_pin_t pin,
				    gpio_flags_t flags)
{
	const struct n32h7_gpio_config *cfg = dev->config;
	uint32_t shift;

	if (!gpio_port_pin_is_supported(cfg->common.port_pin_mask, pin) ||
	    (flags & GPIO_SINGLE_ENDED) != 0U ||
	    ((flags & GPIO_PULL_UP) != 0U && (flags & GPIO_PULL_DOWN) != 0U)) {
		return -EINVAL;
	}

	shift = (uint32_t)pin * 2U;

	cfg->regs->PMODE = (cfg->regs->PMODE & ~(0x3U << shift)) |
			   ((uint32_t)((flags & GPIO_OUTPUT) != 0U) << shift);

	cfg->regs->PUPD &= ~(0x3U << shift);
	if ((flags & GPIO_PULL_UP) != 0U) {
		cfg->regs->PUPD |= 0x1U << shift;
	} else if ((flags & GPIO_PULL_DOWN) != 0U) {
		cfg->regs->PUPD |= 0x2U << shift;
	}

	if ((flags & GPIO_OUTPUT_INIT_HIGH) != 0U) {
		cfg->regs->PBSC = BIT(pin);
	} else if ((flags & GPIO_OUTPUT_INIT_LOW) != 0U) {
		cfg->regs->PBC = BIT(pin);
	}

	return 0;
}

static int n32h7_gpio_get(const struct device *dev, gpio_port_value_t *value)
{
	const struct n32h7_gpio_config *cfg = dev->config;

	*value = cfg->regs->PID;

	return 0;
}

static int n32h7_gpio_set_masked(const struct device *dev, gpio_port_pins_t mask,
				 gpio_port_value_t value)
{
	const struct n32h7_gpio_config *cfg = dev->config;
	gpio_port_pins_t set;
	gpio_port_pins_t clear;

	mask &= cfg->common.port_pin_mask;
	set = value & mask;
	clear = ~value & mask;

	if (set != 0U) {
		cfg->regs->PBSC = set;
	}
	if (clear != 0U) {
		cfg->regs->PBC = clear;
	}

	return 0;
}

static int n32h7_gpio_set_bits(const struct device *dev, gpio_port_pins_t pins)
{
	const struct n32h7_gpio_config *cfg = dev->config;

	pins &= cfg->common.port_pin_mask;
	cfg->regs->PBSC = pins;

	return 0;
}

static int n32h7_gpio_clear_bits(const struct device *dev, gpio_port_pins_t pins)
{
	const struct n32h7_gpio_config *cfg = dev->config;

	pins &= cfg->common.port_pin_mask;
	cfg->regs->PBC = pins;

	return 0;
}

static int n32h7_gpio_toggle(const struct device *dev, gpio_port_pins_t pins)
{
	const struct n32h7_gpio_config *cfg = dev->config;
	gpio_port_pins_t set;
	gpio_port_pins_t clear;

	pins &= cfg->common.port_pin_mask;
	set = ~cfg->regs->PID & pins;
	clear = cfg->regs->PID & pins;

	if (set != 0U) {
		cfg->regs->PBSC = set;
	}
	if (clear != 0U) {
		cfg->regs->PBC = clear;
	}

	return 0;
}

static DEVICE_API(gpio, n32h7_gpio_api) = {
	.pin_configure = n32h7_gpio_pin_configure,
	.port_get_raw = n32h7_gpio_get,
	.port_set_masked_raw = n32h7_gpio_set_masked,
	.port_set_bits_raw = n32h7_gpio_set_bits,
	.port_clear_bits_raw = n32h7_gpio_clear_bits,
	.port_toggle_bits = n32h7_gpio_toggle,
};

static int n32h7_gpio_init(const struct device *dev)
{
	const struct device *clk = DEVICE_DT_GET(DT_NODELABEL(rcc));
	const struct n32h7_gpio_config *cfg = dev->config;

	if (!device_is_ready(clk) || clock_control_on(clk, cfg->clock_subsys) != 0) {
		return -ENODEV;
	}

	return 0;
}

#define N32H7_GPIO_INIT(n)								\
	static const struct n32h7_gpio_config n32h7_gpio_cfg_##n = {			\
		.common = GPIO_COMMON_CONFIG_FROM_DT_INST(n),				\
		.regs = (GPIO_Module *)DT_INST_REG_ADDR(n),				\
		.clock_subsys = (clock_control_subsys_t)(uintptr_t)DT_INST_CLOCKS_CELL(n, clkid), \
	};										\
	static struct gpio_driver_data n32h7_gpio_data_##n;				\
	DEVICE_DT_INST_DEFINE(n, n32h7_gpio_init, NULL, &n32h7_gpio_data_##n,		\
			      &n32h7_gpio_cfg_##n, PRE_KERNEL_1,			\
			      CONFIG_GPIO_INIT_PRIORITY, &n32h7_gpio_api)

DT_INST_FOREACH_STATUS_OKAY(N32H7_GPIO_INIT)
