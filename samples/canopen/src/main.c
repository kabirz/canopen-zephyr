/*
 * Copyright (c) 2026 Kabirz.
 * SPDX-License-Identifier: Apache-2.0
 *
 * CANopen (v4) Zephyr sample - 完整 CANopen 节点.
 *
 * 流程:
 *   1. canopennode_init()       - 协议栈初始化 (CAN 收发已由 CO_driver 完成)
 *   2. storage / leds / sync    - 可选模块
 *   3. main 循环: canopennode_process() 周期驱动
 *   4. CO_RESET_COMM/APP 处理
 *
 * 设备树要求:
 *   chosen { zephyr_canbus = &can1; };
 *   可选 aliases: green_led / red_led (CiA 303-3 指示)
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>

#include "canopennode.h"
#include "OD.h"

#if defined(CONFIG_CANOPENNODE_STORAGE)
#include "storage/CO_storage.h"
#include "canopen_storage.h"
#endif

#if defined(CONFIG_CANOPENNODE_LEDS)
#include "303/CO_LEDs.h"
#include "canopen_leds.h"
#endif

#if defined(CONFIG_CANOPENNODE_SYNC_THREAD)
#include "canopen_sync_thread.h"
#endif

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

#define MAIN_THREAD_SLEEP_US 1000   /* 1ms 主循环 */
#define CANOPEN_NODE_ID      CONFIG_CANOPEN_NODE_ID
#define CANOPEN_BITRATE_KBPS 250

int main(void)
{
	int rc;
	CO_NMT_reset_cmd_t reset;

	LOG_INF("CANopen (v4) sample started, node_id=%d", CANOPEN_NODE_ID);

#if defined(CONFIG_CANOPENNODE_STORAGE)
	CO_storage_t storage;
	CO_storage_entry_t storage_entries[] = {
		{ .addr = &OD_PERSIST_COMM, .len = sizeof(OD_PERSIST_COMM),
		  .subIndexOD = 2, .attr = CO_storage_cmd | CO_storage_restore,
		  .addrNV = NULL },
	};
	uint32_t storage_init_error = 0;
#endif

#if defined(CONFIG_CANOPENNODE_LEDS)
	CO_LEDs_t leds;
#endif

	while (1) {
		rc = canopennode_init(CANOPEN_NODE_ID, CANOPEN_BITRATE_KBPS);
		if (rc != 0) {
			LOG_ERR("canopennode_init failed: %d, retry in 1s", rc);
			k_sleep(K_SECONDS(1));
			continue;
		}

#if defined(CONFIG_CANOPENNODE_STORAGE)
		rc = canopen_storage_init(&storage, CO->CANmodule, OD,
					  storage_entries,
					  ARRAY_SIZE(storage_entries),
					  &storage_init_error);
		if (rc != 0) {
			LOG_WRN("storage init failed: %d", rc);
		}
#endif

#if defined(CONFIG_CANOPENNODE_LEDS)
		if (canopen_leds_init(&leds) != 0) {
			LOG_WRN("LED init failed (ignored)");
		}
#endif

#if defined(CONFIG_CANOPENNODE_SYNC_THREAD)
		canopen_sync_start(CO);
#endif

		/* 通信循环 */
		reset = CO_RESET_NOT;
		while (reset == CO_RESET_NOT) {
			reset = canopennode_process(MAIN_THREAD_SLEEP_US);
			k_sleep(K_USEC(MAIN_THREAD_SLEEP_US));
		}

		LOG_INF("CANopen reset: %s",
			reset == CO_RESET_COMM ? "COMM" : "APP");
		canopennode_shutdown();
	}

	return 0;
}
