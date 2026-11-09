/*
 * Copyright (c) 2026 Nations Technologies
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SOC_NSING_N32H78X_N32H7_HAL_H_
#define ZEPHYR_SOC_NSING_N32H78X_N32H7_HAL_H_

#include <stdint.h>
#include <soc.h>
#include <zephyr/dt-bindings/clock/n32h7_clock.h>

/* The pinctrl backend shares one implementation between the two cores. */
#ifdef CORE_CM4
#define N32H7_CLOCK_GPIOA N32H7_M4_CLOCK_GPIOA
#define N32H7_CLOCK_GPIOB N32H7_M4_CLOCK_GPIOB
#define N32H7_CLOCK_GPIOC N32H7_M4_CLOCK_GPIOC
#define N32H7_CLOCK_GPIOD N32H7_M4_CLOCK_GPIOD
#define N32H7_CLOCK_GPIOE N32H7_M4_CLOCK_GPIOE
#define N32H7_CLOCK_GPIOF N32H7_M4_CLOCK_GPIOF
#define N32H7_CLOCK_GPIOG N32H7_M4_CLOCK_GPIOG
#define N32H7_CLOCK_GPIOH N32H7_M4_CLOCK_GPIOH
#define N32H7_CLOCK_GPIOI N32H7_M4_CLOCK_GPIOI
#define N32H7_CLOCK_GPIOJ N32H7_M4_CLOCK_GPIOJ
#define N32H7_CLOCK_GPIOK N32H7_M4_CLOCK_GPIOK
#define N32H7_CLOCK_AFIO  N32H7_M4_CLOCK_AFIO
#else
#define N32H7_CLOCK_GPIOA N32H7_M7_CLOCK_GPIOA
#define N32H7_CLOCK_GPIOB N32H7_M7_CLOCK_GPIOB
#define N32H7_CLOCK_GPIOC N32H7_M7_CLOCK_GPIOC
#define N32H7_CLOCK_GPIOD N32H7_M7_CLOCK_GPIOD
#define N32H7_CLOCK_GPIOE N32H7_M7_CLOCK_GPIOE
#define N32H7_CLOCK_GPIOF N32H7_M7_CLOCK_GPIOF
#define N32H7_CLOCK_GPIOG N32H7_M7_CLOCK_GPIOG
#define N32H7_CLOCK_GPIOH N32H7_M7_CLOCK_GPIOH
#define N32H7_CLOCK_GPIOI N32H7_M7_CLOCK_GPIOI
#define N32H7_CLOCK_GPIOJ N32H7_M7_CLOCK_GPIOJ
#define N32H7_CLOCK_GPIOK N32H7_M7_CLOCK_GPIOK
#define N32H7_CLOCK_AFIO  N32H7_M7_CLOCK_AFIO
#endif

/* DCMU is the hardware dual-core mailbox used by the N32H7 safety library. */
#define N32H7_DCMU_MSG_CM7_ALIVE 0x00000001U
#define N32H7_DCMU_MSG_CM7_READY 0x00000002U
#define N32H7_DCMU_MSG_CM4_READY 0x00000010U
#define N32H7_DCMU_MSG_CM4_HEARTBEAT 0x00000020U

static inline void n32h7_dcmu_enable(void)
{
#ifdef CORE_CM4
	RCC->CFG4 |= RCC_CFG4_DCMUM4CLKEN;
#else
	RCC->CFG4 |= RCC_CFG4_DCMUM7CLKEN;
#endif
}

static inline void n32h7_dcmu_reset(void)
{
	RCC->CFG4 |= RCC_CFG4_DCMURST;
	RCC->CFG4 &= ~RCC_CFG4_DCMURST;
	__asm__ volatile("dsb" ::: "memory");
}

static inline bool n32h7_dcmu_tx_ready(uint8_t index)
{
	return (DCMU->STS & (1U << (23U - index))) != 0U;
}

static inline void n32h7_dcmu_send(uint8_t index, uint32_t message)
{
	if (n32h7_dcmu_tx_ready(index)) {
		DCMU->TXMSG[index] = message;
	}
}

static inline bool n32h7_dcmu_rx_ready(uint8_t index)
{
	return (DCMU->STS & (1U << (27U - index))) != 0U;
}

static inline uint32_t n32h7_dcmu_receive(uint8_t index)
{
	return DCMU->RCVMSG[index];
}

/* SDK-compatible CM4 release sequence. Call only after a valid CM4 image
 * has been programmed at its independently linked address.
 */
#define N32H7_CM4_BOOTSEL_REG 0x511050ACUL

static inline void n32h7_cm4_release(uint32_t boot_addr)
{
	*(volatile uint32_t *)N32H7_CM4_BOOTSEL_REG = boot_addr;
	RCC->M4RSTREL |= RCC_M4RSTREL_EN;
	__asm__ volatile("dsb" ::: "memory");
	__asm__ volatile("isb" ::: "memory");
}

/* CM7 ownership and peripheral clock gates used by the first drivers. */
static inline void n32h7_clock_enable_gpioa(void)
{
#ifdef CORE_CM4
	RCC->AHB5EN1 |= RCC_AHB5EN1_M4GPIOAEN;
	RCC->AHB5EN2 |= RCC_AHB5EN2_M4AFIOEN;
#else
	RCC->AHB5EN1 |= RCC_AHB5EN1_M7GPIOAEN;
	RCC->AHB5EN2 |= RCC_AHB5EN2_M7AFIOEN;
#endif
}

static inline void n32h7_clock_enable_usart1(void)
{
#ifdef CORE_CM4
	RCC->APB1EN3 |= RCC_APB1EN3_M4USART1EN;
#else
	RCC->APB1EN3 |= RCC_APB1EN3_M7USART1EN;
#endif
}

/* Configure one encoded N32H7 pin.  mode 0 is input, mode 1 is GPIO output,
 * mode 2 is alternate-function output and mode 3 is analog.  open_drain
 * selects an open-drain output pad, as required by I2C and similar buses.
 */
static inline void n32h7_gpio_pin_configure_hw(GPIO_Module *gpio, uint8_t pin, uint8_t af,
					    uint8_t mode, uint8_t pull, bool open_drain)
{
	uint32_t shift = (uint32_t)pin * 2U;
	uint32_t pupd = (uint32_t)pull << shift;

	gpio->PMODE = (gpio->PMODE & ~(0x3U << shift)) |
			       (((uint32_t)mode & 0x3U) << shift);
	gpio->PUPD = (gpio->PUPD & ~(0x3U << shift)) | pupd;
	if (open_drain) {
		gpio->POTYPE |= 1U << pin;
	} else {
		gpio->POTYPE &= ~(1U << pin);
	}
	/* Alternate-function inputs use mode 0 in the compact pinmux format;
	 * they still require the AFR field to be programmed.
	 */
	if (mode == 0U || mode == 2U) {
		uint32_t af_shift = (uint32_t)(pin & 7U) * 4U;
		volatile uint32_t *afr = (pin < 8U) ? &gpio->AFL : &gpio->AFH;
		*afr = (*afr & ~(0xFU << af_shift)) | ((uint32_t)af << af_shift);
	}
}

static inline void n32h7_gpio_pin_af(GPIO_Module *gpio, uint8_t pin, uint8_t af,
					     bool output, bool pull_up)
{
	n32h7_gpio_pin_configure_hw(gpio, pin, af, output ? 2U : 0U, pull_up ? 1U : 0U, false);
}

#endif
