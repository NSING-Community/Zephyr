/*
 * Copyright (c) 2026 Nations Technologies
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT nsing_n32h7_uart

#include <errno.h>
#include <n32h7_hal.h>
#include <n32h7xx_usart.h>
#include <soc.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/irq.h>
#include <zephyr/sys/util.h>

struct n32h7_uart_config {
	const struct pinctrl_dev_config *pcfg;
	uintptr_t base;
	uint32_t clock_id;
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	void (*irq_config_func)(const struct device *dev);
	int irq;
#endif
};

struct n32h7_uart_data {
	struct uart_config uart_cfg;
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	uart_irq_callback_user_data_t cb;
	void *cb_data;
	bool tx_fifo_filled;
#endif
};

static int n32h7_uart_apply_config(const struct device *dev, const struct uart_config *cfg)
{
	const struct n32h7_uart_config *config = dev->config;
	USART_Module *uart = (USART_Module *)config->base;
	USART_InitType init = {0};

	if (cfg->baudrate == 0U) {
		return -EINVAL;
	}
	switch (cfg->parity) {
	case UART_CFG_PARITY_NONE:
		init.Parity = USART_PE_NO;
		break;
	case UART_CFG_PARITY_ODD:
		init.Parity = USART_PE_ODD;
		break;
	case UART_CFG_PARITY_EVEN:
		init.Parity = USART_PE_EVEN;
		break;
	default:
		return -ENOTSUP;
	}
	if (cfg->parity == UART_CFG_PARITY_NONE) {
		switch (cfg->data_bits) {
		case UART_CFG_DATA_BITS_8:
			init.WordLength = USART_WL_8B;
			break;
		case UART_CFG_DATA_BITS_9:
			init.WordLength = USART_WL_9B;
			break;
		default:
			return -ENOTSUP;
		}
	} else {
		switch (cfg->data_bits) {
		case UART_CFG_DATA_BITS_7:
			init.WordLength = USART_WL_8B;
			break;
		case UART_CFG_DATA_BITS_8:
			init.WordLength = USART_WL_9B;
			break;
		default:
			return -ENOTSUP;
		}
	}
	switch (cfg->stop_bits) {
	case UART_CFG_STOP_BITS_0_5:
		init.StopBits = USART_STPB_0_5;
		break;
	case UART_CFG_STOP_BITS_1:
		init.StopBits = USART_STPB_1;
		break;
	case UART_CFG_STOP_BITS_1_5:
		init.StopBits = USART_STPB_1_5;
		break;
	case UART_CFG_STOP_BITS_2:
		init.StopBits = USART_STPB_2;
		break;
	default:
		return -ENOTSUP;
	}
	switch (cfg->flow_ctrl) {
	case UART_CFG_FLOW_CTRL_NONE:
		init.HardwareFlowControl = USART_HFCTRL_NONE;
		break;
	case UART_CFG_FLOW_CTRL_RTS_CTS:
		init.HardwareFlowControl = USART_HFCTRL_RTS_CTS;
		break;
	default:
		return -ENOTSUP;
	}

	init.BaudRate = cfg->baudrate;
	init.Mode = USART_MODE_RX | USART_MODE_TX;
	init.OverSampling = USART_16OVER;
	USART_Enable(uart, DISABLE);
	USART_Init(uart, &init);
	USART_CfgRxFIFOThreshold(uart, USART_FIFO_DEEP1);
	USART_FIFOModeSet(uart, ENABLE);
	USART_ClrFIFO(uart);
	USART_Enable(uart, ENABLE);
	return 0;
}

static int n32h7_uart_poll_in(const struct device *dev, unsigned char *c)
{
	const struct n32h7_uart_config *cfg = dev->config;
	USART_Module *uart = (USART_Module *)cfg->base;

	if (USART_GetFlagStatus(uart, USART_FLAG_RXDNE) == RESET) {
		return -1;
	}
	*c = (uint8_t)USART_ReceiveData(uart);
	return 0;
}

static void n32h7_uart_poll_out(const struct device *dev, unsigned char c)
{
	const struct n32h7_uart_config *cfg = dev->config;
	USART_Module *uart = (USART_Module *)cfg->base;

	while (USART_GetFlagStatus(uart, USART_FLAG_TXFF) == SET) {
	}
	USART_SendData(uart, c);
}

static int n32h7_uart_err_check(const struct device *dev)
{
	const struct n32h7_uart_config *cfg = dev->config;
	USART_Module *uart = (USART_Module *)cfg->base;
	int errors = 0;

	if (USART_GetFlagStatus(uart, USART_FLAG_OREF) == SET) {
		errors |= UART_ERROR_OVERRUN;
	}
	if (USART_GetFlagStatus(uart, USART_FLAG_NEF) == SET) {
		errors |= UART_ERROR_NOISE;
	}
	if (USART_GetFlagStatus(uart, USART_FLAG_PEF) == SET) {
		errors |= UART_ERROR_PARITY;
	}
	if (USART_GetFlagStatus(uart, USART_FLAG_FEF) == SET) {
		errors |= UART_ERROR_FRAMING;
	}

	/* N32 clears these flags after an STS read followed by a DAT read. */
	if (errors != 0) {
		(void)USART_ReceiveData(uart);
	}

	return errors;
}

#ifdef CONFIG_UART_INTERRUPT_DRIVEN
static int n32h7_uart_fifo_fill(const struct device *dev, const uint8_t *tx_data, int len)
{
	const struct n32h7_uart_config *cfg = dev->config;
	struct n32h7_uart_data *data = dev->data;
	USART_Module *uart = (USART_Module *)cfg->base;
	int count = MIN(len, 7);

	for (int i = 0; i < count; i++) {
		USART_SendData(uart, tx_data[i]);
	}
	if (count > 0) {
		data->tx_fifo_filled = true;
	}
	return count;
}

static int n32h7_uart_fifo_read(const struct device *dev, uint8_t *rx_data, const int len)
{
	const struct n32h7_uart_config *cfg = dev->config;
	USART_Module *uart = (USART_Module *)cfg->base;
	/* Read the live FIFO count: bytes keep arriving while the callback
	 * runs, and a count snapshotted at interrupt entry leaves the rest in
	 * the FIFO until the next interrupt.  A burst longer than the FIFO can
	 * then overflow it before that interrupt is served.
	 */
	int count = MIN(len, (int)USART_GetRxFIFO_Num(uart));

	for (int i = 0; i < count; i++) {
		rx_data[i] = (uint8_t)USART_ReceiveData(uart);
	}

	return count;
}

static void n32h7_uart_irq_tx_enable(const struct device *dev)
{
	const struct n32h7_uart_config *cfg = dev->config;
	USART_Module *uart = (USART_Module *)cfg->base;

	USART_ConfigInt(uart, USART_INT_TXFTE, ENABLE);
	irq_enable(cfg->irq);
	/* H78 does not raise a new IRQ when TXFT was already set before its
	 * interrupt was enabled. Kick the first callback; subsequent bytes are
	 * driven by the hardware TXFT transition.
	 */
	if (USART_GetFlagStatus(uart, USART_FLAG_TXFT) == SET) {
		k_irq_set_pending(cfg->irq);
	}
}

static void n32h7_uart_irq_tx_disable(const struct device *dev)
{
	const struct n32h7_uart_config *cfg = dev->config;
	const struct n32h7_uart_data *data = dev->data;
	USART_Module *uart = (USART_Module *)cfg->base;

	/* USART_ConfigInt() performs a read-modify-write on FIFO. If it follows
	 * the final DAT writes before they cross into the peripheral clock
	 * domain, those bytes can be lost. Wait until the queued frames leave the
	 * FIFO before changing its interrupt-enable bits.
	 */
	if (data->tx_fifo_filled) {
		while (USART_GetFlagStatus(uart, USART_FLAG_TXC) == RESET) {
		}
	}
	USART_ConfigInt(uart, USART_INT_TXFTE, DISABLE);
}

static int n32h7_uart_irq_tx_ready(const struct device *dev)
{
	const struct n32h7_uart_config *cfg = dev->config;
	const struct n32h7_uart_data *data = dev->data;
	USART_Module *uart = (USART_Module *)cfg->base;

	return !data->tx_fifo_filled && ((uart->FIFO & USART_FIFO_TXFTIEN) != 0U) &&
	       (USART_GetFlagStatus(uart, USART_FLAG_TXFT) == SET);
}

static void n32h7_uart_irq_rx_enable(const struct device *dev)
{
	const struct n32h7_uart_config *cfg = dev->config;

	USART_ConfigInt((USART_Module *)cfg->base, USART_INT_RXFTE, ENABLE);
	irq_enable(cfg->irq);
}

static void n32h7_uart_irq_rx_disable(const struct device *dev)
{
	const struct n32h7_uart_config *cfg = dev->config;

	USART_ConfigInt((USART_Module *)cfg->base, USART_INT_RXFTE, DISABLE);
}

static int n32h7_uart_irq_rx_ready(const struct device *dev)
{
	const struct n32h7_uart_config *cfg = dev->config;
	USART_Module *uart = (USART_Module *)cfg->base;

	return ((uart->FIFO & USART_FIFO_RXFTIEN) != 0U) && (USART_GetRxFIFO_Num(uart) > 0U);
}

static int n32h7_uart_irq_tx_complete(const struct device *dev)
{
	const struct n32h7_uart_config *cfg = dev->config;

	return USART_GetFlagStatus((USART_Module *)cfg->base, USART_FLAG_TXC) == SET;
}

static int n32h7_uart_irq_is_pending(const struct device *dev)
{
	const struct n32h7_uart_config *cfg = dev->config;
	const struct n32h7_uart_data *data = dev->data;
	USART_Module *uart = (USART_Module *)cfg->base;
	uint32_t status = uart->STS;

	return (((uart->FIFO & USART_FIFO_RXFTIEN) != 0U) && (USART_GetRxFIFO_Num(uart) > 0U)) ||
	       (!data->tx_fifo_filled && ((uart->FIFO & USART_FIFO_TXFTIEN) != 0U) &&
		((status & USART_STS_TXFT) != 0U)) ||
	       (((uart->CTRL1 & USART_CTRL1_PEIEN) != 0U) && ((status & USART_STS_PEF) != 0U)) ||
	       (((uart->CTRL3 & USART_CTRL3_ERRIEN) != 0U) &&
		((status & (USART_STS_OREF | USART_STS_NEF | USART_STS_FEF)) != 0U));
}

static void n32h7_uart_irq_update(const struct device *dev)
{
	ARG_UNUSED(dev);
}

static void n32h7_uart_irq_err_enable(const struct device *dev)
{
	USART_Module *uart = (USART_Module *)((const struct n32h7_uart_config *)dev->config)->base;

	USART_ConfigInt(uart, USART_INT_PEF, ENABLE);
	USART_ConfigInt(uart, USART_INT_ERRF, ENABLE);
}

static void n32h7_uart_irq_err_disable(const struct device *dev)
{
	USART_Module *uart = (USART_Module *)((const struct n32h7_uart_config *)dev->config)->base;

	USART_ConfigInt(uart, USART_INT_PEF, DISABLE);
	USART_ConfigInt(uart, USART_INT_ERRF, DISABLE);
}

static void n32h7_uart_irq_callback_set(const struct device *dev, uart_irq_callback_user_data_t cb,
					void *user_data)
{
	struct n32h7_uart_data *data = dev->data;

	data->cb = cb;
	data->cb_data = user_data;
}

static void n32h7_uart_isr(const struct device *dev)
{
	struct n32h7_uart_data *data = dev->data;

	data->tx_fifo_filled = false;
	if (data->cb != NULL) {
		data->cb(dev, data->cb_data);
	}
}

#endif

/*
 * Runtime configuration follows the same validation rules as the G45
 * driver. This also makes the driver usable by the generic UART shell and
 * tests when CONFIG_UART_USE_RUNTIME_CONFIGURE is enabled.
 */
#ifdef CONFIG_UART_USE_RUNTIME_CONFIGURE
static int n32h7_uart_configure(const struct device *dev, const struct uart_config *cfg)
{
	struct n32h7_uart_data *data = dev->data;
	int ret = n32h7_uart_apply_config(dev, cfg);

	if (ret == 0) {
		data->uart_cfg = *cfg;
	}
	return ret;
}

static int n32h7_uart_config_get(const struct device *dev, struct uart_config *cfg)
{
	*cfg = ((const struct n32h7_uart_data *)dev->data)->uart_cfg;
	return 0;
}
#endif

static int n32h7_uart_init(const struct device *dev)
{
	const struct n32h7_uart_config *cfg = dev->config;
	struct n32h7_uart_data *data = dev->data;
	const struct device *clk = DEVICE_DT_GET(DT_NODELABEL(rcc));
	int ret;

	if (!device_is_ready(clk) ||
	    clock_control_on(clk, (clock_control_subsys_t)(uintptr_t)cfg->clock_id) != 0) {
		return -ENODEV;
	}
	if (pinctrl_apply_state(cfg->pcfg, PINCTRL_STATE_DEFAULT) < 0) {
		return -EIO;
	}

	ret = n32h7_uart_apply_config(dev, &data->uart_cfg);
	if (ret < 0) {
		return ret;
	}
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	cfg->irq_config_func(dev);
#endif
	return 0;
}

static DEVICE_API(uart, n32h7_uart_api) = {
	.poll_in = n32h7_uart_poll_in,
	.poll_out = n32h7_uart_poll_out,
	.err_check = n32h7_uart_err_check,
#ifdef CONFIG_UART_USE_RUNTIME_CONFIGURE
	.configure = n32h7_uart_configure,
	.config_get = n32h7_uart_config_get,
#endif
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	.fifo_fill = n32h7_uart_fifo_fill,
	.fifo_read = n32h7_uart_fifo_read,
	.irq_tx_enable = n32h7_uart_irq_tx_enable,
	.irq_tx_disable = n32h7_uart_irq_tx_disable,
	.irq_tx_ready = n32h7_uart_irq_tx_ready,
	.irq_rx_enable = n32h7_uart_irq_rx_enable,
	.irq_rx_disable = n32h7_uart_irq_rx_disable,
	.irq_rx_ready = n32h7_uart_irq_rx_ready,
	.irq_tx_complete = n32h7_uart_irq_tx_complete,
	.irq_err_enable = n32h7_uart_irq_err_enable,
	.irq_err_disable = n32h7_uart_irq_err_disable,
	.irq_is_pending = n32h7_uart_irq_is_pending,
	.irq_update = n32h7_uart_irq_update,
	.irq_callback_set = n32h7_uart_irq_callback_set,
#endif
};

#ifdef CONFIG_UART_INTERRUPT_DRIVEN
#define N32H7_UART_IRQ_DECL(n) static void n32h7_uart_irq_config_##n(const struct device *dev);
#define N32H7_UART_IRQ_CONFIG(n)                                                                   \
	.irq_config_func = n32h7_uart_irq_config_##n, .irq = DT_INST_IRQN(n),
#define N32H7_UART_IRQ_CONNECT(n)                                                                  \
	static void n32h7_uart_irq_config_##n(const struct device *dev)                            \
	{                                                                                          \
		ARG_UNUSED(dev);                                                                   \
		IRQ_CONNECT(DT_INST_IRQN(n), DT_INST_IRQ(n, priority), n32h7_uart_isr,             \
			    DEVICE_DT_INST_GET(n), 0);                                             \
	}
#else
#define N32H7_UART_IRQ_DECL(n)
#define N32H7_UART_IRQ_CONFIG(n)
#define N32H7_UART_IRQ_CONNECT(n)
#endif

#define N32H7_UART_INIT(n)                                                                         \
	PINCTRL_DT_INST_DEFINE(n);                                                                 \
	N32H7_UART_IRQ_DECL(n)                                                                     \
	static struct n32h7_uart_data n32h7_uart_data_##n = {                                      \
		.uart_cfg =                                                                        \
			{                                                                          \
				.baudrate = DT_INST_PROP_OR(n, current_speed, 115200),             \
				.parity = DT_INST_ENUM_IDX_OR(n, parity, UART_CFG_PARITY_NONE),    \
				.stop_bits =                                                       \
					DT_INST_ENUM_IDX_OR(n, stop_bits, UART_CFG_STOP_BITS_1),   \
				.data_bits = DT_INST_PROP_OR(n, data_bits, 8) - 5,                 \
				.flow_ctrl = DT_INST_PROP_OR(n, hw_flow_control, 0)                \
						     ? UART_CFG_FLOW_CTRL_RTS_CTS                  \
						     : UART_CFG_FLOW_CTRL_NONE,                    \
			},                                                                         \
	};                                                                                         \
	static const struct n32h7_uart_config n32h7_uart_cfg_##n = {                               \
		.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(n),                                         \
		.base = DT_INST_REG_ADDR(n),                                                       \
		.clock_id = DT_INST_CLOCKS_CELL(n, clkid),                                         \
		N32H7_UART_IRQ_CONFIG(n)};                                                         \
	DEVICE_DT_INST_DEFINE(n, n32h7_uart_init, NULL, &n32h7_uart_data_##n, &n32h7_uart_cfg_##n, \
			      PRE_KERNEL_1, CONFIG_SERIAL_INIT_PRIORITY, &n32h7_uart_api);         \
	N32H7_UART_IRQ_CONNECT(n)

DT_INST_FOREACH_STATUS_OKAY(N32H7_UART_INIT)
