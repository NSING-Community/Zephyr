/*
 * Copyright (c) 2026 Nations Technologies
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/devicetree.h>
#include <zephyr/dt-bindings/pinctrl/n32h7-pinctrl.h>
#include <zephyr/sys/util.h>
#include <soc.h>
#include <n32h7_hal.h>

struct n32h7_pinctrl_port {
	GPIO_Module *gpio;
	uintptr_t clock;
	uint8_t pins;
};

int pinctrl_configure_pins(const pinctrl_soc_pin_t *pins, uint8_t cnt, uintptr_t reg)
{
	/* GPIOK is the only port with eight pins; every other port has sixteen. */
	static const struct n32h7_pinctrl_port ports[] = {
		{GPIOA, (uintptr_t)N32H7_CLOCK_GPIOA, 16U},
		{GPIOB, (uintptr_t)N32H7_CLOCK_GPIOB, 16U},
		{GPIOC, (uintptr_t)N32H7_CLOCK_GPIOC, 16U},
		{GPIOD, (uintptr_t)N32H7_CLOCK_GPIOD, 16U},
		{GPIOE, (uintptr_t)N32H7_CLOCK_GPIOE, 16U},
		{GPIOF, (uintptr_t)N32H7_CLOCK_GPIOF, 16U},
		{GPIOG, (uintptr_t)N32H7_CLOCK_GPIOG, 16U},
		{GPIOH, (uintptr_t)N32H7_CLOCK_GPIOH, 16U},
		{GPIOI, (uintptr_t)N32H7_CLOCK_GPIOI, 16U},
		{GPIOJ, (uintptr_t)N32H7_CLOCK_GPIOJ, 16U},
		{GPIOK, (uintptr_t)N32H7_CLOCK_GPIOK, 8U},
	};
	const struct device *rcc = DEVICE_DT_GET(DT_NODELABEL(rcc));

	ARG_UNUSED(reg);

	if (!device_is_ready(rcc)) {
		return -ENODEV;
	}

	if (clock_control_on(rcc, (clock_control_subsys_t)(uintptr_t)N32H7_CLOCK_AFIO) != 0) {
		return -EIO;
	}

	for (uint8_t i = 0; i < cnt; i++) {
		uint32_t v = pins[i];
		uint8_t port = (v >> 16) & 0xffU;
		uint8_t pin = (v >> 8) & 0xffU;
		uint8_t af = (v >> 4) & 0xfU;
		uint8_t mode = (v >> 2) & 0x3U;
		uint8_t pull = v & 0x3U;
		bool open_drain = (v & N32H7_PIN_OPEN_DRAIN) != 0U;
		const struct n32h7_pinctrl_port *entry;

		if (port >= ARRAY_SIZE(ports)) {
			return -ENOTSUP;
		}

		entry = &ports[port];
		if (pin >= entry->pins) {
			return -EINVAL;
		}

		if (clock_control_on(rcc, (clock_control_subsys_t)entry->clock) != 0) {
			return -EIO;
		}

		n32h7_gpio_pin_configure_hw(entry->gpio, pin, af, mode, pull, open_drain);
	}

	return 0;
}
