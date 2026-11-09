/*
 * Copyright (c) 2026 Nations Technologies
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/mbox.h>
#include <zephyr/fatal.h>
#include <zephyr/linker/sections.h>
#include <zephyr/sys/printk.h>
#include <soc.h>
#include <n32h7_shared.h>
#include "n32h7_hal.h"

extern volatile uint32_t n32h78x_boot_stage;
extern volatile uint32_t n32h78x_wake_count;
extern volatile uint32_t n32h78x_reset_cause;
extern volatile uint32_t n32h78x_reset_count;

/* First-fault snapshot retained across a watchdog/system reset.  This is
 * intentionally in the bring-up sample so silicon validation can inspect the
 * original exception instead of the secondary fault in arch_system_halt().
 */
Z_GENERIC_SECTION(.noinit) volatile uint32_t n32h7_fault_reason;
Z_GENERIC_SECTION(.noinit) volatile uint32_t n32h7_fault_pc;
Z_GENERIC_SECTION(.noinit) volatile uint32_t n32h7_fault_lr;
Z_GENERIC_SECTION(.noinit) volatile uint32_t n32h7_fault_cfsr;
Z_GENERIC_SECTION(.noinit) volatile uint32_t n32h7_fault_hfsr;
Z_GENERIC_SECTION(.noinit) volatile uint32_t n32h7_fault_bfar;

#if defined(CONFIG_MBOX_N32H7_DCMU)
static volatile uint32_t dcmu_irq_count;
static volatile uint32_t dcmu_irq_payload;

static void n32h7_dcmu_mbox_cb(const struct device *dev, uint32_t channel,
		void *user_data, struct mbox_msg *msg)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(channel);
	ARG_UNUSED(user_data);

	if (msg != NULL && msg->data != NULL && msg->size == sizeof(uint32_t)) {
		dcmu_irq_payload = *(const uint32_t *)msg->data;
		dcmu_irq_count++;
	#ifdef CORE_CM4
		volatile struct n32h7_shared_ctrl *shared = n32h7_shared_ctrl();

		shared->reserved[0] = dcmu_irq_count;
		shared->reserved[1] = dcmu_irq_payload;
		(void)sys_cache_data_flush_range((void *)shared, sizeof(*shared));
	#endif
	}
}
#endif

static void n32h7_print_reset_cause(uint32_t cause)
{
	printk("reset cause:");
	if (cause & RCC_CTRLSTS_IWDG2RSTF) {
		printk(" IWDG2");
	}
	if (cause & RCC_CTRLSTS_IWDG1RSTF) {
		printk(" IWDG1");
	}
	if (cause & RCC_CTRLSTS_WWDG2RSTF) {
		printk(" WWDG2");
	}
	if (cause & RCC_CTRLSTS_WWDG1RSTF) {
		printk(" WWDG1");
	}
	if (cause & RCC_CTRLSTS_CM4SFTRSTF) {
		printk(" CM4_SOFT");
	}
	if (cause & RCC_CTRLSTS_CM7SFTRSTF) {
		printk(" CM7_SOFT");
	}
	if (cause & RCC_CTRLSTS_PORRSTF) {
		printk(" POR");
	}
	if (cause & RCC_CTRLSTS_PINRSTF) {
		printk(" PIN");
	}
	if ((cause & (RCC_CTRLSTS_IWDG1RSTF | RCC_CTRLSTS_IWDG2RSTF |
			     RCC_CTRLSTS_WWDG1RSTF | RCC_CTRLSTS_WWDG2RSTF |
			     RCC_CTRLSTS_CM4SFTRSTF | RCC_CTRLSTS_CM7SFTRSTF |
			     RCC_CTRLSTS_PORRSTF | RCC_CTRLSTS_PINRSTF)) == 0U) {
		printk(" UNKNOWN");
	}
	printk("\n");
}

void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *esf)
{
	n32h7_fault_reason = reason;
	n32h7_fault_cfsr = SCB->CFSR;
	n32h7_fault_hfsr = SCB->HFSR;
	n32h7_fault_bfar = SCB->BFAR;
	if (esf != NULL) {
		n32h7_fault_pc = esf->basic.pc;
		n32h7_fault_lr = esf->basic.lr;
	}
	k_fatal_halt(reason);
}

#ifdef CONFIG_UART_N32H7_INTERRUPT
static void n32h7_uart_echo_cb(const struct device *dev, void *user_data)
{
	uint8_t buf[8];

	ARG_UNUSED(user_data);

	uart_irq_update(dev);
	while (uart_irq_is_pending(dev)) {
		if (uart_irq_rx_ready(dev)) {
			int n = uart_fifo_read(dev, buf, sizeof(buf));

			if (n > 0) {
				volatile struct n32h7_shared_ctrl *shared = n32h7_shared_ctrl();

				shared->reserved[0] += (uint32_t)n;
				shared->reserved[1] = buf[0];
				GPIOA->POD ^= BIT(2);
				for (int i = 0; i < n; i++) {
					uint8_t ch = buf[i];

					uart_poll_out(dev, ch);
					if (ch == '\r') {
						uart_poll_out(dev, '\n');
					}
				}
			}
		} else {
			break;
		}
	}
}
#endif

int main(void)
{
	n32h78x_boot_stage = 0x4E333205U; /* scheduler test main entered */
	#ifdef CONFIG_UART_N32H7_INTERRUPT
	n32h7_shared_ctrl()->reserved[0] = 0U;
	#endif
	#if !defined(CORE_CM4) || defined(CONFIG_UART_CONSOLE)
	const struct device *gpioa = DEVICE_DT_GET(DT_NODELABEL(gpioa));
	#endif
	#ifdef CONFIG_UART_CONSOLE
	const struct device *console = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
	#else
	const struct device *console = NULL;

	ARG_UNUSED(console);
	#endif
	#if defined(CONFIG_WDT_N32H7_IWDG)
	#ifdef CORE_CM4
	const struct device *watchdog = DEVICE_DT_GET(DT_NODELABEL(iwdg2));
	#else
	const struct device *watchdog = DEVICE_DT_GET(DT_NODELABEL(iwdg1));
	#endif
	#define WATCHDOG_NAME "IWDG"
	#endif
	printk("RCC_CTRLSTS(saved)=0x%08x current=0x%08x\n",
	       n32h78x_reset_cause, RCC->CTRLSTS);
	n32h7_print_reset_cause(n32h78x_reset_cause);
	printk("reset_count=%u\n", n32h78x_reset_count);
	printk("shared reset: CM4=0x%08x CM7=0x%08x gen=%u\n",
	       n32h7_shared_ctrl()->cm4_reset_cause,
	       n32h7_shared_ctrl()->cm7_reset_cause,
	       n32h7_shared_ctrl()->reset_generation);
	#if !defined(CORE_CM4) || defined(CONFIG_UART_CONSOLE)
	printk("N32H7 CM7 bring-up: GPIOA=%s console=%s\n",
	       device_is_ready(gpioa) ? "ready" : "not-ready",
	       device_is_ready(console) ? "ready" : "not-ready");
	#endif
	#ifdef CONFIG_UART_N32H7_INTERRUPT
	if (device_is_ready(console)) {
		uart_irq_callback_user_data_set(console, n32h7_uart_echo_cb, NULL);
		uart_irq_rx_enable(console);
		printk("console: RX interrupt echo enabled\n");
	}
	#endif
	#if defined(CONFIG_N32H78X_DCMU_TEST)
	printk("DCMU: status-before=0x%08x\n", DCMU->STS);
	const struct device *dcmu = DEVICE_DT_GET(DT_NODELABEL(dcmu));
	int ret = mbox_register_callback(dcmu, 0U, n32h7_dcmu_mbox_cb, NULL);

	if (ret == 0) {
		ret = mbox_set_enabled(dcmu, 0U, true);
	}
	#ifndef CORE_CM4
	struct mbox_msg msg = {
		.data = &(uint32_t){ N32H7_DCMU_MSG_CM7_ALIVE },
		.size = sizeof(uint32_t),
	};
	if (ret == 0) {
		ret = mbox_send(dcmu, 0U, &msg);
	}
	#endif
	printk("DCMU mbox: register/enable/send=%d\n", ret);
	uint32_t waited_ms = 0U;

	while (dcmu_irq_count == 0U && waited_ms < 1000U) {
		k_sleep(K_MSEC(10));
		waited_ms += 10U;
	}
	printk("DCMU mbox: irq-count=%u payload=0x%08x\n",
	       dcmu_irq_count, dcmu_irq_payload);
	#ifndef CORE_CM4
	(void)sys_cache_data_invd_range((void *)n32h7_shared_ctrl(),
					 sizeof(*n32h7_shared_ctrl()));
	printk("DCMU shared: CM4 irq-count=%u payload=0x%08x\n",
	       n32h7_shared_ctrl()->reserved[0],
	       n32h7_shared_ctrl()->reserved[1]);
	#endif
	#ifdef CORE_CM4
	if (ret == 0 && dcmu_irq_payload == N32H7_DCMU_MSG_CM7_ALIVE) {
		struct mbox_msg reply = {
			.data = &(uint32_t){ N32H7_DCMU_MSG_CM4_READY },
			.size = sizeof(uint32_t),
		};
		ret = mbox_send(dcmu, 0U, &reply);
		printk("DCMU mbox: CM4 reply=%d\n", ret);
	}
	#endif
	#endif
	#if defined(CONFIG_WDT_N32H7_IWDG)
	if (device_is_ready(watchdog)) {
		struct wdt_timeout_cfg wdt_cfg = {
			.window = {
				.min = 0,
				.max = 5000,
			},
			.callback = NULL,
			.flags = WDT_FLAG_RESET_SOC,
		};
		int wdt_chan = wdt_install_timeout(watchdog, &wdt_cfg);
		int wdt_ret = (wdt_chan >= 0) ? wdt_setup(watchdog, 0) : wdt_chan;

		printk("%s: install=%d setup=%d\n", WATCHDOG_NAME, wdt_chan, wdt_ret);
		if (wdt_ret == 0) {
			(void)wdt_feed(watchdog, 0);
		}
	} else {
		printk("%s: not-ready\n", WATCHDOG_NAME);
	}
	#endif
	#if !defined(CORE_CM4) || defined(CONFIG_UART_CONSOLE)
	int led_cfg[3];

	led_cfg[0] = gpio_pin_configure(gpioa, 2, GPIO_OUTPUT_INACTIVE);
	led_cfg[1] = gpio_pin_configure(gpioa, 3, GPIO_OUTPUT_INACTIVE);
	led_cfg[2] = gpio_pin_configure(gpioa, 4, GPIO_OUTPUT_INACTIVE);
	printk("GPIO config: PA2=%d PA3=%d PA4=%d\n",
	       led_cfg[0], led_cfg[1], led_cfg[2]);
	if (!device_is_ready(gpioa) || led_cfg[0] < 0 || led_cfg[1] < 0 || led_cfg[2] < 0) {
		printk("GPIO LED setup failed\n");
		return 0;
	}
	printk("GPIO LED test: PA2/PA3/PA4\n");
	#endif
	/* .noinit is intentionally retained for early-boot diagnostics, but a
	 * fresh application run should start its scheduler counter at zero.
	 */
	n32h78x_wake_count = 0U;
	for (;;) {
		static uint8_t pattern;
		#if defined(CONFIG_WDT_N32H7_IWDG)
		static uint8_t feed_count;
		#endif
		#ifndef CORE_CM4
		static uint8_t heartbeat_report_div;
		#endif
		#ifdef CONFIG_N32H78X_CM4_SUPERVISOR
		static uint32_t last_cm4_heartbeat;
		static uint8_t cm4_stale_cycles;
		#endif
		#if !defined(CONFIG_UART_N32H7_INTERRUPT) && (!defined(CORE_CM4) || defined(CONFIG_UART_CONSOLE))
		(void)gpio_pin_set(gpioa, 2, (pattern >> 0) & 1U);
		(void)gpio_pin_set(gpioa, 3, (pattern >> 1) & 1U);
		(void)gpio_pin_set(gpioa, 4, (pattern >> 2) & 1U);
		#endif
		#if !defined(CONFIG_UART_N32H7_INTERRUPT) && (!defined(CORE_CM4) || defined(CONFIG_UART_CONSOLE))
		printk("GPIO LED pattern: %u (PA4..PA2=%u%u%u)\n", pattern,
		       (pattern >> 2) & 1U, (pattern >> 1) & 1U, pattern & 1U);
		#endif
		#ifdef CORE_CM4
		/* Exercise the cache-safe shared control block continuously. */
		n32h7_shared_cm4_heartbeat();
		#else
		/* Report occasionally so the UART remains readable while proving
		 * CM7 observes CM4 writes over an extended run.
		 */
		if (++heartbeat_report_div == 8U) {
			volatile struct n32h7_shared_ctrl *shared = n32h7_shared_ctrl();
			#ifdef CONFIG_N32H78X_CM4_SUPERVISOR
			uint32_t cm4_heartbeat;
			(void)sys_cache_data_invd_range((void *)shared, sizeof(*shared));
			cm4_heartbeat = shared->cm4_heartbeat;
			if (cm4_heartbeat == last_cm4_heartbeat) {
				if (cm4_stale_cycles < UINT8_MAX) {
					cm4_stale_cycles++;
				}
				if (cm4_stale_cycles >= CONFIG_N32H78X_CM4_HEARTBEAT_TIMEOUT_CYCLES) {
					printk("CM4 supervisor: heartbeat timeout (%u cycles)\n",
					       cm4_stale_cycles);
				}
			} else {
				cm4_stale_cycles = 0U;
			}
			last_cm4_heartbeat = cm4_heartbeat;
			#endif
			printk("Dual-core heartbeat: CM4=%u CM7=%u\n",
			       shared->cm4_heartbeat, shared->cm7_state);
			heartbeat_report_div = 0U;
		}
		#endif
		pattern = (pattern + 1U) & 0x7U;
		#if defined(CONFIG_WDT_N32H7_IWDG)
		if (device_is_ready(watchdog)) {
			bool feed_stopped = false;

		#if defined(CONFIG_N32H7_IWDG_RESET_TEST)
			feed_stopped = (feed_count >= 6U);
		#endif
			if (feed_stopped) {
				printk("%s: feed stopped; reset expected\n", WATCHDOG_NAME);
			} else {
				int feed_ret = wdt_feed(watchdog, 0);

				#ifndef CONFIG_UART_N32H7_INTERRUPT
				printk("%s: feed=%d\n", WATCHDOG_NAME, feed_ret);
				#else
				ARG_UNUSED(feed_ret);
				#endif
				feed_count++;
			}
		}
		#endif
		#if defined(CORE_CM4) && !defined(CONFIG_UART_CONSOLE)
		/* Keep the CM4 watchdog regression independent of the CM4 SysTick
		 * scheduler path; this also allows testing very early bring-up.
		 */
		k_busy_wait(500000);
		#else
		k_sleep(K_MSEC(500));
		#endif
		n32h78x_wake_count++;
	}
}
