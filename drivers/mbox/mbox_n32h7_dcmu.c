/*
 * Copyright (c) 2026 Nations Technologies
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT nsing_n32h7_dcmu

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/mbox.h>
#include <zephyr/irq.h>
#include <zephyr/sys/util.h>

#include <n32h7_hal.h>

#define N32H7_DCMU_CHANNELS 4U

struct n32h7_dcmu_data {
	/* MBOX callbacks are registered per receive channel. */
	mbox_callback_t callback[N32H7_DCMU_CHANNELS];
	void *user_data[N32H7_DCMU_CHANNELS];
	/* Keep the peripheral IRQ enabled while at least one RX channel is on. */
	uint8_t enabled_mask;
};

static void n32h7_dcmu_isr(const struct device *dev)
{
	struct n32h7_dcmu_data *data = dev->data;
	DCMU_Module *regs = DCMU;
	uint32_t pending;
	uint32_t enabled = 0U;

	/* The SDK's interrupt handler treats RFF as the source and clears the
	 * corresponding GPIF latch after consuming the receive register.  The
	 * latter is important on N32H7: leaving GPIF set can suppress subsequent
	 * cross-core notifications even though RFF is read correctly.
	 */
	pending = regs->STS & (DCMU_STS_RFF0 | DCMU_STS_RFF1 |
			       DCMU_STS_RFF2 | DCMU_STS_RFF3);

	/* Do not dispatch stale FIFO flags after a channel was disabled. */
	for (uint32_t channel = 0U; channel < N32H7_DCMU_CHANNELS; channel++) {
		if ((data->enabled_mask & BIT(channel)) != 0U) {
			enabled |= BIT(27U - channel);
		}
	}
	pending &= enabled;

	for (uint32_t channel = 0U; channel < N32H7_DCMU_CHANNELS; channel++) {
		if ((pending & BIT(27U - channel)) != 0U) {
			uint32_t payload = regs->RCVMSG[channel];
			uint32_t status = regs->STS;
			struct mbox_msg msg = { .data = &payload, .size = sizeof(payload) };

			/* GPIF is write-one-to-clear; preserve the other status bits as
			 * required by the vendor SDK implementation.
			 */
			status &= ~(DCMU_STS_GPIF0 | DCMU_STS_GPIF1 |
				    DCMU_STS_GPIF2 | DCMU_STS_GPIF3);
			status |= BIT(31U - channel);
			regs->STS = status;

			if (data->callback[channel] != NULL) {
				data->callback[channel](dev, channel, data->user_data[channel], &msg);
			}
		}
	}
}

static int n32h7_dcmu_mbox_send(const struct device *dev, mbox_channel_id_t channel,
				const struct mbox_msg *msg)
{
	ARG_UNUSED(dev);

	if (channel >= N32H7_DCMU_CHANNELS || msg == NULL || msg->data == NULL ||
	    msg->size != sizeof(uint32_t)) {
		return -EINVAL;
	}

	if (!n32h7_dcmu_tx_ready((uint8_t)channel)) {
		return -EBUSY;
	}

	DCMU->TXMSG[channel] = *(const uint32_t *)msg->data;

	return 0;
}

static int n32h7_dcmu_register_callback(const struct device *dev,
					mbox_channel_id_t channel, mbox_callback_t cb,
					void *user_data)
{
	struct n32h7_dcmu_data *data = dev->data;

	if (channel >= N32H7_DCMU_CHANNELS) {
		return -EINVAL;
	}

	data->callback[channel] = cb;
	data->user_data[channel] = user_data;

	return 0;
}

static int n32h7_dcmu_set_enabled(const struct device *dev, mbox_channel_id_t channel,
				  bool enable)
{
	struct n32h7_dcmu_data *data = dev->data;
	uint32_t bit;

	if (channel >= N32H7_DCMU_CHANNELS) {
		return -EINVAL;
	}
	if (enable == ((data->enabled_mask & BIT(channel)) != 0U)) {
		return -EALREADY;
	}

	bit = BIT(27U - channel);
	if (enable) {
		DCMU->CTRL |= bit;
		data->enabled_mask |= BIT(channel);
		irq_enable(DT_INST_IRQN(0));
	} else {
		DCMU->CTRL &= ~bit;
		data->enabled_mask &= ~BIT(channel);
		if (data->enabled_mask == 0U) {
			irq_disable(DT_INST_IRQN(0));
		}
	}

	return 0;
}

static int n32h7_dcmu_init(const struct device *dev)
{
	ARG_UNUSED(dev);

	n32h7_dcmu_enable();
	/* Do not drain RFF here. CM7 can release CM4 and send the first boot
	 * handshake before CM4 reaches POST_KERNEL. Preserving a pending FIFO
	 * entry lets the application consume that message after callback setup.
	 */
	IRQ_CONNECT(DT_INST_IRQN(0), DT_INST_IRQ(0, priority),
		    n32h7_dcmu_isr, DEVICE_DT_INST_GET(0), 0);
	irq_disable(DT_INST_IRQN(0));

	return 0;
}

static int n32h7_dcmu_mtu(const struct device *dev)
{
	ARG_UNUSED(dev);

	return sizeof(uint32_t);
}

static uint32_t n32h7_dcmu_max_channels(const struct device *dev)
{
	ARG_UNUSED(dev);

	return N32H7_DCMU_CHANNELS;
}

static DEVICE_API(mbox, n32h7_dcmu_api) = {
	.send = n32h7_dcmu_mbox_send,
	.register_callback = n32h7_dcmu_register_callback,
	.mtu_get = n32h7_dcmu_mtu,
	.max_channels_get = n32h7_dcmu_max_channels,
	.set_enabled = n32h7_dcmu_set_enabled,
};

static struct n32h7_dcmu_data n32h7_dcmu_data;

DEVICE_DT_INST_DEFINE(0, n32h7_dcmu_init, NULL, &n32h7_dcmu_data, NULL,
		      POST_KERNEL, CONFIG_MBOX_INIT_PRIORITY, &n32h7_dcmu_api);
