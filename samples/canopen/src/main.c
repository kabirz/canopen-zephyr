/*
 * Copyright (c) 2026 Kabirz.
 * SPDX-License-Identifier: Apache-2.0
 *
 * CANopen (v4) Zephyr sample - 最小 CANopen 节点.
 *
 * 流程:
 *   1. canopennode_init(node_id, bitrate)  - 初始化协议栈
 *   2. main 循环: canopennode_process() 周期驱动 SDO/em/NMT/HB/LSS
 *   3. 收到 CO_RESET_COMM: 重初始化; CO_RESET_APP: 全量重启
 *
 * 骨架阶段: 协议栈初始化可走通 (stub CO_driver 不真正收发 CAN).
 * 后续 CO_driver 完善后即可与真实 CANopen master 通信.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "canopennode.h"

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

#define MAIN_THREAD_SLEEP_US 1000   /* 1ms, 与 SYNC thread 周期一致 */
#define CANOPEN_NODE_ID      CONFIG_CANOPEN_NODE_ID
#define CANOPEN_BITRATE_KBPS 250

int main(void)
{
	int rc;
	CO_NMT_reset_cmd_t reset;

	LOG_INF("CANopen (v4) sample 启动, node_id=%d", CANOPEN_NODE_ID);

	while (1) {
		rc = canopennode_init(CANOPEN_NODE_ID, CANOPEN_BITRATE_KBPS);
		if (rc != 0) {
			LOG_ERR("canopennode_init 失败: %d, 1s 后重试", rc);
			k_sleep(K_SECONDS(1));
			continue;
		}

		/* 通信循环 */
		reset = CO_RESET_NOT;
		while (reset == CO_RESET_NOT) {
			reset = canopennode_process(MAIN_THREAD_SLEEP_US);
			k_sleep(K_USEC(MAIN_THREAD_SLEEP_US));
		}

		/* CO_RESET_COMM: 回到外层 while, 重初始化通信.
		 * CO_RESET_APP: 同样重初始化 (本 sample 不区分 app/comm 重启) */
		LOG_INF("CANopen reset: %s",
			reset == CO_RESET_COMM ? "COMM" : "APP");
		canopennode_shutdown();
	}

	return 0;
}
