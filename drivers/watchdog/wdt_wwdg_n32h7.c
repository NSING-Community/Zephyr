/*
 * Copyright (c) 2026 Nations Technologies
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT nsing_n32h7_wwdg

#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/irq.h>
#include <zephyr/sys/clock.h>
#include <zephyr/sys/util.h>
#include <errno.h>
#include <soc.h>
#include <n32h7xx_rcc.h>

#define N32_WWDG_COUNTER_MIN      0x40U
#define N32_WWDG_COUNTER_MAX      0x3fffU
#define N32_WWDG_ENABLE_BIT       BIT(14)
#define N32_WWDG_EWI_BIT          BIT(16)
#define N32_WWDG_EWI_FLAG         BIT(0)
#define N32_WWDG_PRESCALER_SHIFT  14U
#define N32_WWDG_INTERNAL_DIVIDER 4096U

struct n32_wwdg_config {
	const struct device *clock;
	uint32_t clock_id;
	WWDG_Module *regs;
	int irq;
	void (*irq_config)(const struct device *dev);
};

struct n32_wwdg_data {
	uint16_t counter;
	wdt_callback_t callback;
	bool timeout_installed;
	bool started;
};

static void n32_wwdg_isr(const struct device *dev)
{
	const struct n32_wwdg_config *config = dev->config;
	struct n32_wwdg_data *data = dev->data;

	if ((config->regs->STS & N32_WWDG_EWI_FLAG) != 0U) {
		config->regs->STS = 0U;
		if (data->callback != NULL) {
			data->callback(dev, 0);
		}
	}
}

static int n32_wwdg_install_timeout(const struct device *dev, const struct wdt_timeout_cfg *cfg)
{
	const struct n32_wwdg_config *config = dev->config;
	struct n32_wwdg_data *data = dev->data;
	RCC_ClocksTypeDef clocks;
	uint32_t pclk;
	uint32_t prescaler;
	uint64_t divider;
	uint64_t ticks;
	uint32_t counter = 0U;
	uint32_t window;

	if (data->started) {
		return -EBUSY;
	}
	if (cfg->window.max == 0U || cfg->window.min >= cfg->window.max ||
	    ((cfg->flags & WDT_FLAG_RESET_MASK) != WDT_FLAG_RESET_SOC)) {
		return -EINVAL;
	}
	if (data->timeout_installed) {
		return -ENOMEM;
	}
	/* The WWDG counter runs from PCLK1 on both instances, even though
	 * WWDG1's reset and clock gate live in the AXI register group.
	 */
	RCC_GetClocksFreqValue(&clocks);
	pclk = clocks.APB1ClkFreq;
	if (pclk == 0U) {
		return -EIO;
	}

	for (prescaler = 0U; prescaler <= 3U; prescaler++) {
		divider = ((uint64_t)N32_WWDG_INTERNAL_DIVIDER << prescaler) * MSEC_PER_SEC;
		ticks = DIV_ROUND_UP((uint64_t)cfg->window.max * pclk, divider);
		if (ticks >= 1U && ticks <= (N32_WWDG_COUNTER_MAX - N32_WWDG_COUNTER_MIN + 1U)) {
			counter = N32_WWDG_COUNTER_MIN + ticks - 1U;
			break;
		}
	}
	if (counter == 0U) {
		return -EINVAL;
	}

	if (cfg->window.min == 0U) {
		/* No minimum window: allow a refresh at any counter value. */
		window = N32_WWDG_COUNTER_MAX;
	} else {
		ticks = DIV_ROUND_UP((uint64_t)cfg->window.min * pclk, divider);
		if (ticks >= (counter - N32_WWDG_COUNTER_MIN + 1U)) {
			return -EINVAL;
		}
		window = counter - ticks + 1U;
	}

	config->regs->CFG = (prescaler << N32_WWDG_PRESCALER_SHIFT) | window;
	if (cfg->callback != NULL) {
		data->callback = cfg->callback;
	}
	data->counter = counter;
	data->timeout_installed = true;
	return 0;
}

#if defined(CORE_CM4)
#define N32_WWDG1_DBG_FZ   (&DBG->M4APB6FZ)
#define N32_WWDG2_DBG_FZ   (&DBG->M4APB1FZ)
#define N32_WWDG1_DBG_STOP DBG_M4APB6FZ_WWDG1_STOP
#define N32_WWDG2_DBG_STOP DBG_M4APB1FZ_WWDG2_STOP
#else
#define N32_WWDG1_DBG_FZ   (&DBG->M7APB6FZ)
#define N32_WWDG2_DBG_FZ   (&DBG->M7APB1FZ)
#define N32_WWDG1_DBG_STOP DBG_M7APB6FZ_WWDG1_STOP
#define N32_WWDG2_DBG_STOP DBG_M7APB1FZ_WWDG2_STOP
#endif

static void n32_wwdg_set_debug_pause(const struct n32_wwdg_config *config, bool pause)
{
	bool is_wwdg1 = (config->regs == WWDG1);
	volatile uint32_t *fz = is_wwdg1 ? N32_WWDG1_DBG_FZ : N32_WWDG2_DBG_FZ;
	uint32_t stop = is_wwdg1 ? N32_WWDG1_DBG_STOP : N32_WWDG2_DBG_STOP;

	/* The CMSIS DBG stop fields are bit masks, not bit indices. */
	if (pause) {
		*fz |= stop;
	} else {
		*fz &= ~stop;
	}
}

static int n32_wwdg_setup(const struct device *dev, uint8_t options)
{
	const struct n32_wwdg_config *config = dev->config;
	struct n32_wwdg_data *data = dev->data;

	if ((options & ~WDT_OPT_PAUSE_HALTED_BY_DBG) != 0U) {
		return -ENOTSUP;
	}
	if (data->started) {
		return -EBUSY;
	}
	if (!data->timeout_installed) {
		return -EINVAL;
	}
	n32_wwdg_set_debug_pause(config, (options & WDT_OPT_PAUSE_HALTED_BY_DBG) != 0U);
	config->regs->STS = 0U;
	NVIC_ClearPendingIRQ(config->irq);
	/* EWINT has to be programmed while the watchdog is still stopped. It is
	 * latched by the activation write, so enabling it after ACTB leaves the
	 * early-wakeup interrupt permanently silent while the reset still fires.
	 */
	if (data->callback != NULL) {
		config->regs->CFG |= N32_WWDG_EWI_BIT;
		irq_enable(config->irq);
	}
	config->regs->CTRL = N32_WWDG_ENABLE_BIT | data->counter;
	data->started = true;
	return 0;
}

static int n32_wwdg_disable(const struct device *dev)
{
	ARG_UNUSED(dev);
	return -EPERM;
}

static int n32_wwdg_feed(const struct device *dev, int channel_id)
{
	const struct n32_wwdg_config *config = dev->config;
	struct n32_wwdg_data *data = dev->data;

	if (channel_id != 0) {
		return -EINVAL;
	}
	if (!data->started) {
		return -EINVAL;
	}
	config->regs->CTRL = data->counter;
	return 0;
}

static int n32_wwdg_init(const struct device *dev)
{
	const struct n32_wwdg_config *config = dev->config;
	int ret = clock_control_on(config->clock,
				   (clock_control_subsys_t)(uintptr_t)config->clock_id);

	if (ret != 0) {
		return ret;
	}
	/* Route expiry to NRST, matching the SDK WWDG_Reset examples. */
	if (config->regs == WWDG1) {
		RCC->CFG1 |= RCC_CFG1_WWDG1RSTEN;
	} else {
		RCC->CFG1 |= RCC_CFG1_WWDG2RSTEN;
	}
	config->regs->CFG &= ~N32_WWDG_EWI_BIT;
	config->irq_config(dev);
	return 0;
}

static DEVICE_API(wdt, n32_wwdg_api) = {
	.setup = n32_wwdg_setup,
	.disable = n32_wwdg_disable,
	.install_timeout = n32_wwdg_install_timeout,
	.feed = n32_wwdg_feed,
};

#if defined(CONFIG_SOC_SERIES_N32H78X)
#define N32_WWDG_CLOCK_ID(n) DT_INST_CLOCKS_CELL(n, clkid)
#else
#define N32_WWDG_CLOCK_ID(n) DT_INST_CLOCKS_CELL(n, bits)
#endif

#define N32_WWDG_INIT(n)                                                                           \
	static struct n32_wwdg_data n32_wwdg_data_##n;                                             \
	static void n32_wwdg_irq_config_##n(const struct device *dev);                             \
	static const struct n32_wwdg_config n32_wwdg_config_##n = {                                \
		.clock = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(n)),                                    \
		.clock_id = N32_WWDG_CLOCK_ID(n),                                                  \
		.regs = (WWDG_Module *)DT_INST_REG_ADDR(n),                                        \
		.irq = DT_INST_IRQN(n),                                                            \
		.irq_config = n32_wwdg_irq_config_##n,                                             \
	};                                                                                         \
	static void n32_wwdg_irq_config_##n(const struct device *dev)                              \
	{                                                                                          \
		const struct n32_wwdg_config *config = dev->config;                                \
		config->regs->STS = 0U;                                                            \
		IRQ_CONNECT(DT_INST_IRQN(n), DT_INST_IRQ(n, priority), n32_wwdg_isr,               \
			    DEVICE_DT_INST_GET(n), 0);                                             \
		NVIC_ClearPendingIRQ(DT_INST_IRQN(n));                                             \
		irq_disable(DT_INST_IRQN(n));                                                      \
	}                                                                                          \
	DEVICE_DT_INST_DEFINE(n, n32_wwdg_init, NULL, &n32_wwdg_data_##n, &n32_wwdg_config_##n,    \
			      POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &n32_wwdg_api)

DT_INST_FOREACH_STATUS_OKAY(N32_WWDG_INIT)
