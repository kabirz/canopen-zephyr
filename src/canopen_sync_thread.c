/*
 * Copyright (c) 2026 Kabirz.
 * SPDX-License-Identifier: Apache-2.0
 *
 * CANopen SYNC 线程 (v4).
 *
 * 独立线程周期处理 CANopen SYNC 相关的 RPDO/TPDO, 与 main 循环解耦.
 * 周期 = CONFIG_CANOPENNODE_SYNC_THREAD_PERIOD_US (默认 1ms).
 *
 * 参考 CANopenNodeZephyr 的 canopen_sync.c 设计, 适配 v4 CO_process/CO_SYNC.
 */

#if defined(CONFIG_CANOPENNODE_SYNC_THREAD)

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "CANopen.h"
#include "301/CO_driver.h"

LOG_MODULE_REGISTER(canopennode_sync, CONFIG_CANOPENNODE_LOG_LEVEL);

/* SYNC 线程对象指针 (由 canopen_sync_start() 设置) */
static CO_t *sync_co;

/*
 * SYNC 线程入口: 周期驱动 CO_process().
 * 需要在 main 线程中由应用调用 canopen_sync_start() 启动.
 */
static void canopen_sync_thread(void *arg1, void *arg2, void *arg3)
{
	CO_t *co = (CO_t *)arg1;
	uint32_t time_diff_us = CONFIG_CANOPENNODE_SYNC_THREAD_PERIOD_US;
	uint32_t timer_next_us = 0;

	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	while (co != NULL) {
		/* v4 CO_process 驱动所有对象 (含 SYNC 相关 PDO) */
		CO_process(co, false, time_diff_us, &timer_next_us);

		k_sleep(K_USEC(CONFIG_CANOPENNODE_SYNC_THREAD_PERIOD_US));
	}
}

K_THREAD_STACK_DEFINE(canopen_sync_stack,
		      CONFIG_CANOPENNODE_SYNC_THREAD_STACK_SIZE);

static struct k_thread canopen_sync_thread_data;

/*
 * 启动 SYNC 线程. 在 CO 初始化完成后调用.
 *
 * @param co CANopen 对象 (CO_t*)
 * @return 0=成功
 */
int canopen_sync_start(CO_t *co)
{
	if (co == NULL) {
		return -EINVAL;
	}
	sync_co = co;

	k_thread_create(&canopen_sync_thread_data, canopen_sync_stack,
			K_THREAD_STACK_SIZEOF(canopen_sync_stack),
			canopen_sync_thread, co, NULL, NULL,
			CONFIG_CANOPENNODE_SYNC_THREAD_PRIORITY, 0,
			K_NO_WAIT);
	k_thread_name_set(&canopen_sync_thread_data, "canopen_sync");

	LOG_INF("CANopen SYNC thread started (period %d us)",
		CONFIG_CANOPENNODE_SYNC_THREAD_PERIOD_US);
	return 0;
}

#endif /* CONFIG_CANOPENNODE_SYNC_THREAD */
