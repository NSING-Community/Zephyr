/*
 * Copyright (c) 2026 Nations Technologies
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT nsing_n32h7_iwdg

#include <zephyr/drivers/watchdog.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/clock.h>
#include <zephyr/sys/util.h>
#include <errno.h>
#include <soc.h>

#define N32_IWDG_KEY_ENABLE       0xCCCCU
#define N32_IWDG_KEY_RELOAD       0xAAAAU
#define N32_IWDG_KEY_WRITE_ENABLE 0x5555U
#define N32_IWDG_RELOAD_MAX       0x0fffU
#define N32_IWDG_PRESCALER_MAX    6U
#define N32_IWDG_PVU_FLAG         BIT(0)
#define N32_IWDG_CRVU_FLAG        BIT(1)

struct n32_iwdg_config {
	IWDG_Module *regs;
	const struct device *clock;
	clock_control_subsys_t clock_id;
};

struct n32_iwdg_data {
	uint16_t reload;
	uint8_t prescaler;
	bool timeout_installed;
	bool started;
};

/* Wait until a pending PREDIV or RELV update has reached the watchdog. */
static bool n32_iwdg_wait_update(IWDG_Module *regs, uint32_t flag)
{
	uint32_t start = k_uptime_get_32();

	while ((regs->STS & flag) != 0U) {
		if ((k_uptime_get_32() - start) > 100U) {
			return false;
		}
	}

	return true;
}

static int n32_iwdg_install_timeout(const struct device *dev, const struct wdt_timeout_cfg *cfg)
{
	struct n32_iwdg_data *data = dev->data;
	uint64_t ticks;
	uint32_t divider = 4U;
	uint8_t prescaler = 0U;
	uint32_t reload;
	uint32_t lsi_hz = DT_PROP(DT_NODELABEL(clk_lsi), clock_frequency);

	if (cfg->window.max == 0U) {
		return -EINVAL;
	}
	if ((cfg->callback != NULL) || (cfg->window.min != 0U) ||
	    ((cfg->flags & WDT_FLAG_RESET_MASK) != WDT_FLAG_RESET_SOC)) {
		return -ENOTSUP;
	}
	if (data->started) {
		return -EBUSY;
	}
	if (data->timeout_installed) {
		return -ENOMEM;
	}

	ticks = (uint64_t)cfg->window.max * lsi_hz / MSEC_PER_SEC;
	while ((ticks / divider) > (N32_IWDG_RELOAD_MAX + 1U) &&
	       prescaler < N32_IWDG_PRESCALER_MAX) {
		prescaler++;
		divider <<= 1U;
	}
	if (ticks < divider || (ticks / divider) > (N32_IWDG_RELOAD_MAX + 1U)) {
		return -EINVAL;
	}

	reload = (uint32_t)(ticks / divider) - 1U;
	data->prescaler = prescaler;
	data->reload = reload;
	data->timeout_installed = true;
	return 0;
}

static int n32_iwdg_setup(const struct device *dev, uint8_t options)
{
	const struct n32_iwdg_config *config = dev->config;
	struct n32_iwdg_data *data = dev->data;
	uint32_t start;

	if (options != 0U) {
		return -ENOTSUP;
	}
	if (data->started) {
		return -EBUSY;
	}
	if (!data->timeout_installed) {
		return -EINVAL;
	}
	if (config->clock != NULL && clock_control_on(config->clock, config->clock_id) != 0) {
		return -EIO;
	}
	RCC->BDCTRL |= RCC_BDCTRL_LSIEN | RCC_BDCTRL_LSIRDEN;
	start = k_uptime_get_32();
	while ((RCC->BDCTRL & RCC_BDCTRL_LSIRDF) == 0U) {
		if ((k_uptime_get_32() - start) > 100U) {
			return -EIO;
		}
	}

	config->regs->KEY = N32_IWDG_KEY_WRITE_ENABLE;
	if (!n32_iwdg_wait_update(config->regs, N32_IWDG_PVU_FLAG)) {
		return -EIO;
	}
	config->regs->PREDIV = data->prescaler;
	if (!n32_iwdg_wait_update(config->regs, N32_IWDG_PVU_FLAG)) {
		return -EIO;
	}
	/* A write to RELV is ignored while a previous reload value update is
	 * still in flight.
	 */
	if (!n32_iwdg_wait_update(config->regs, N32_IWDG_CRVU_FLAG)) {
		return -EIO;
	}
	config->regs->RELV = data->reload;
	if (!n32_iwdg_wait_update(config->regs, N32_IWDG_CRVU_FLAG)) {
		return -EIO;
	}
	config->regs->KEY = N32_IWDG_KEY_ENABLE;
	config->regs->KEY = N32_IWDG_KEY_RELOAD;
	data->started = true;
	return 0;
}

static int n32_iwdg_disable(const struct device *dev)
{
	ARG_UNUSED(dev);
	return -EPERM;
}

static int n32_iwdg_feed(const struct device *dev, int channel_id)
{
	const struct n32_iwdg_config *config = dev->config;

	if (channel_id != 0) {
		return -EINVAL;
	}

	config->regs->KEY = N32_IWDG_KEY_RELOAD;

	return 0;
}

static DEVICE_API(wdt, n32_iwdg_api) = {
	.setup = n32_iwdg_setup,
	.disable = n32_iwdg_disable,
	.install_timeout = n32_iwdg_install_timeout,
	.feed = n32_iwdg_feed,
};

#if defined(CONFIG_SOC_SERIES_N32H78X)
#define N32_IWDG_CLOCK_ID(n) DT_INST_CLOCKS_CELL(n, clkid)
#else
#define N32_IWDG_CLOCK_ID(n) 0U
#endif

#define N32_IWDG_INIT(n)                                                                           \
	static struct n32_iwdg_data n32_iwdg_data_##n;                                             \
	static const struct n32_iwdg_config n32_iwdg_config_##n = {                                \
		.regs = (IWDG_Module *)DT_INST_REG_ADDR(n),                                        \
		.clock = COND_CODE_1(DT_INST_NODE_HAS_PROP(n, clocks), \
			(DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(n))), (NULL)),                           \
			 .clock_id = (clock_control_subsys_t)(uintptr_t)N32_IWDG_CLOCK_ID(n),      \
		};                                                                                 \
	DEVICE_DT_INST_DEFINE(n, NULL, NULL, &n32_iwdg_data_##n, &n32_iwdg_config_##n,             \
			      POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &n32_iwdg_api)

DT_INST_FOREACH_STATUS_OKAY(N32_IWDG_INIT)
