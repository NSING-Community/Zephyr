/*
 * Copyright (c) 2026 Nations Technologies
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_PINCTRL_N32H7_PINCTRL_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_PINCTRL_N32H7_PINCTRL_H_

/*
 * One N32H7 pinmux cell:
 *
 *     bits  0..1   pull configuration
 *     bits  2..3   mode
 *     bits  4..7   alternate function number
 *     bits  8..15  pin number
 *     bits 16..23  port, 0 = GPIOA
 *     bit  24      open-drain output (0 = push-pull)
 */

#define N32H7_PIN_INPUT  0U
#define N32H7_PIN_OUTPUT 1U
#define N32H7_PIN_AF     2U
#define N32H7_PIN_ANALOG 3U

#define N32H7_PULL_NONE 0U
#define N32H7_PULL_UP   1U
#define N32H7_PULL_DOWN 2U

#define N32H7_PIN_OPEN_DRAIN (1U << 24)

#define N32H7_PINMUX(port, pin, af, mode, pull)                                    \
	((((port) - 'A') << 16) | ((pin) << 8) | ((af) << 4) | ((mode) << 2) |     \
	 (pull))

#define N32H7_PINMUX_OD(port, pin, af, mode, pull)                                 \
	(N32H7_PINMUX(port, pin, af, mode, pull) | N32H7_PIN_OPEN_DRAIN)

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_PINCTRL_N32H7_PINCTRL_H_ */
