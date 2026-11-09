/*
 * Copyright (c) 2026 Nations Technologies
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_N32H78X_PINCTRL_SOC_H_
#define ZEPHYR_N32H78X_PINCTRL_SOC_H_
#include <zephyr/devicetree.h>
#include <zephyr/types.h>
typedef uint32_t pinctrl_soc_pin_t;
#define N32H7_PIN(port, pin, af, mode, pull) \
	(((port) << 16) | ((pin) << 8) | ((af) << 4) | ((mode) << 2) | (pull))
#define Z_PINCTRL_STATE_PIN_INIT(node_id, prop, idx) \
	((pinctrl_soc_pin_t)DT_PROP_BY_IDX(DT_PHANDLE_BY_IDX(node_id, prop, idx), pinmux, 0)),
#define Z_PINCTRL_STATE_PINS_INIT(node_id, prop) \
	{ DT_FOREACH_PROP_ELEM(node_id, prop, Z_PINCTRL_STATE_PIN_INIT) }
#endif
