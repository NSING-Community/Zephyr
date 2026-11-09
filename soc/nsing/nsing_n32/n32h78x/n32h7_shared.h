/*
 * Copyright (c) 2026 Nations Technologies
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SOC_NSING_NSING_N32_N32H78X_N32H7_SHARED_H_
#define ZEPHYR_SOC_NSING_NSING_N32_N32H78X_N32H7_SHARED_H_

#include <stdint.h>
#include <zephyr/cache.h>

/* The DTS marks this 4 KiB window as non-cacheable on both cores.  Keep the
 * barriers below nevertheless: they order the producer's writes before the
 * other core observes the state transition.
 */
#define N32H7_SHARED_BASE 0x3001F000UL
#define N32H7_SHARED_MAGIC 0x4E333253UL
#define N32H7_SHARED_VERSION 2U

struct n32h7_shared_ctrl {
	uint32_t magic;
	uint32_t version;
	uint32_t cm4_state;
	uint32_t cm4_heartbeat;
	/* Keep CM7-owned state on a separate 32-byte cache line.  The two
	 * cores may otherwise write back stale copies of the same line and
	 * silently overwrite the other core's state.
	 */
	uint32_t reserved[4];
	uint32_t cm7_state;
	uint32_t reserved2[3];
	/* Reset diagnostics are independent from the live state cache lines. */
	uint32_t cm4_reset_cause;
	uint32_t cm7_reset_cause;
	uint32_t reset_generation;
	uint32_t reserved3;
};

#define N32H7_CM4_STATE_RESET 0U
#define N32H7_CM4_STATE_RUNNING 1U
#define N32H7_CM4_STATE_READY 2U
#define N32H7_CM4_STATE_ERROR 0xEEU
#define N32H7_CM7_STATE_READY 1U
#define N32H7_CM7_STATE_RESET 0U

static inline volatile struct n32h7_shared_ctrl *n32h7_shared_ctrl(void)
{
	return (volatile struct n32h7_shared_ctrl *)N32H7_SHARED_BASE;
}

static inline void n32h7_shared_publish_cm7_ready(void)
{
	volatile struct n32h7_shared_ctrl *shared = n32h7_shared_ctrl();

	shared->magic = N32H7_SHARED_MAGIC;
	shared->version = N32H7_SHARED_VERSION;
	shared->cm7_state = N32H7_CM7_STATE_READY;
	(void)sys_cache_data_flush_range((void *)shared, sizeof(*shared));
	__asm__ volatile("dmb sy" ::: "memory");
}

static inline void n32h7_shared_reset_for_cm7(void)
{
	volatile struct n32h7_shared_ctrl *shared = n32h7_shared_ctrl();
	uint32_t generation = (shared->magic == N32H7_SHARED_MAGIC &&
			       shared->version == N32H7_SHARED_VERSION) ? shared->reset_generation + 1U : 0U;

	shared->magic = N32H7_SHARED_MAGIC;
	shared->version = N32H7_SHARED_VERSION;
	shared->cm4_state = N32H7_CM4_STATE_RESET;
	shared->cm4_heartbeat = 0U;
	shared->cm7_state = N32H7_CM7_STATE_RESET;
	shared->reserved[0] = 0U;
	shared->reserved[1] = 0U;
	shared->reserved[2] = 0U;
	shared->reserved[3] = 0U;
	shared->reserved2[0] = 0U;
	shared->reserved2[1] = 0U;
	shared->reserved2[2] = 0U;
	shared->reset_generation = generation;
	if (generation == 0U) {
		shared->cm4_reset_cause = 0U;
		shared->cm7_reset_cause = 0U;
	}
	(void)sys_cache_data_flush_range((void *)shared, sizeof(*shared));
	__asm__ volatile("dmb sy" ::: "memory");
}

static inline void n32h7_shared_publish_cm4_reset_cause(uint32_t cause)
{
	volatile struct n32h7_shared_ctrl *shared = n32h7_shared_ctrl();

	shared->cm4_reset_cause = cause;
	(void)sys_cache_data_flush_range((void *)&shared->cm4_reset_cause,
					 sizeof(shared->cm4_reset_cause));
	__asm__ volatile("dmb sy" ::: "memory");
}

static inline void n32h7_shared_publish_cm7_reset_cause(uint32_t cause)
{
	volatile struct n32h7_shared_ctrl *shared = n32h7_shared_ctrl();

	shared->cm7_reset_cause = cause;
	(void)sys_cache_data_flush_range((void *)&shared->cm7_reset_cause,
					 sizeof(shared->cm7_reset_cause));
	__asm__ volatile("dmb sy" ::: "memory");
}

static inline bool n32h7_shared_cm7_is_ready(void)
{
	volatile struct n32h7_shared_ctrl *shared = n32h7_shared_ctrl();

	(void)sys_cache_data_invd_range((void *)shared, sizeof(*shared));
	__asm__ volatile("dmb sy" ::: "memory");
	return shared->magic == N32H7_SHARED_MAGIC &&
	       shared->version == N32H7_SHARED_VERSION &&
	       shared->cm7_state == N32H7_CM7_STATE_READY;
}

static inline void n32h7_shared_mark_cm4_running(void)
{
	volatile struct n32h7_shared_ctrl *shared = n32h7_shared_ctrl();

	shared->magic = N32H7_SHARED_MAGIC;
	shared->version = N32H7_SHARED_VERSION;
	shared->cm4_state = N32H7_CM4_STATE_RUNNING;
	shared->cm4_heartbeat = 0U;
	(void)sys_cache_data_flush_range((void *)shared, sizeof(*shared));
	__asm__ volatile("dmb sy" ::: "memory");
}

static inline void n32h7_shared_cm4_heartbeat(void)
{
	volatile struct n32h7_shared_ctrl *shared = n32h7_shared_ctrl();

	shared->cm4_heartbeat++;
	(void)sys_cache_data_flush_range((void *)shared, sizeof(*shared));
	__asm__ volatile("dmb sy" ::: "memory");
}

static inline void n32h7_shared_mark_cm4_error(void)
{
	volatile struct n32h7_shared_ctrl *shared = n32h7_shared_ctrl();

	shared->magic = N32H7_SHARED_MAGIC;
	shared->version = N32H7_SHARED_VERSION;
	shared->cm4_state = N32H7_CM4_STATE_ERROR;
	(void)sys_cache_data_flush_range((void *)shared, sizeof(*shared));
	__asm__ volatile("dmb sy" ::: "memory");
}

#endif
