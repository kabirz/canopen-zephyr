/*
 * Copyright (c) 2026 Kabirz.
 * SPDX-License-Identifier: Apache-2.0
 *
 * canopennode.h 应用层 API 实现 (Zephyr v4 包装).
 *
 * 参考 v4 example/main_blank.c, 隐藏 CO_new/CO_CANopenInit 复杂参数.
 * 骨架阶段: 接口完整可编译, 但 CO_driver 是 stub 不会真正收发 CAN.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>

#include "CANopen.h"
#include "OD.h"
#include "canopennode.h"

LOG_MODULE_REGISTER(canopennode, CONFIG_CANOPENNODE_LOG_LEVEL);

/* 全局 CO 对象 (单例) */
CO_t *CO = NULL;

/* 默认参数 (与 v4 example/main_blank.c 一致) */
#define NMT_CONTROL                                                                                \
	(CO_NMT_STARTUP_TO_OPERATIONAL | CO_NMT_ERR_ON_ERR_REG | CO_ERR_REG_GENERIC_ERR |         \
	 CO_ERR_REG_COMMUNICATION)
#define FIRST_HB_TIME_MS        500
#define SDO_SRV_TIMEOUT_TIME_MS 1000
#define SDO_CLI_TIMEOUT_TIME_MS 500
#define SDO_CLI_BLOCK_TRANSFER  false
#define OD_STATUS_BITS          NULL

/* CAN 设备: 从 devicetree chosen(zephyr_canbus) 取. 骨架阶段不实际使用. */
#define CAN_NODE DEVICE_DT_GET(DT_CHOSEN(zephyr_canbus))

int canopennode_init(uint8_t node_id, uint16_t bitrate_kbps)
{
	uint32_t heap_used = 0;
	CO_ReturnError_t err;
	uint32_t err_info = 0;

	if (node_id < 1 || node_id > 127) {
		return -EINVAL;
	}

	/* 分配 CO 对象 (无 CO_MULTIPLE_OD, 用 OD.h 静态计数) */
	CO = CO_new(NULL, &heap_used);
	if (CO == NULL) {
		LOG_ERR("CO_new 失败 (out of memory)");
		return -ENOMEM;
	}
	LOG_INF("CO_new: %u bytes", heap_used);

	/* CANptr: 骨架阶段传 NULL, 后续替换为 DEVICE_DT_GET(zephyr_canbus) */
	void *CANptr = (void *)CAN_NODE;
	uint16_t pending_bitrate = bitrate_kbps;
	uint8_t pending_node_id = node_id;

	/* 通信重置段 (参考 v4 example main 的内层循环) */
	CO->CANmodule->CANnormal = false;
	CO_CANsetConfigurationMode(CANptr);
	err = CO_CANmodule_init(CO->CANmodule, CANptr, CO->CANmodule->rxArray,
				CO->CANmodule->rxSize, CO->CANmodule->txArray,
				CO->CANmodule->txSize, pending_bitrate);
	if (err != CO_ERROR_NO) {
		LOG_ERR("CO_CANmodule_init 失败: %d", err);
		CO_DELETE(CO);
		return -EIO;
	}

	err = CO_CANopenInit(CO, CO->NMT, CO->em, OD, OD_STATUS_BITS, NMT_CONTROL,
			     FIRST_HB_TIME_MS, SDO_SRV_TIMEOUT_TIME_MS,
			     SDO_CLI_TIMEOUT_TIME_MS, SDO_CLI_BLOCK_TRANSFER,
			     pending_node_id, &err_info);
	if (err != CO_ERROR_NO && err != CO_ERROR_NODE_ID_UNCONFIGURED_LSS) {
		LOG_ERR("CO_CANopenInit 失败: %d (err_info=%u)", err, err_info);
		CO_DELETE(CO);
		return -EIO;
	}

	err = CO_CANopenInitPDO(CO, CO->em, OD, pending_node_id, &err_info);
	if (err != CO_ERROR_NO) {
		LOG_ERR("CO_CANopenInitPDO 失败: %d (err_info=%u)", err, err_info);
		CO_DELETE(CO);
		return -EIO;
	}

	/* 进入正常通信 */
	CO_CANsetNormalMode(CO->CANmodule);

	LOG_INF("CANopen 初始化完成 (node_id=%u, bitrate=%u kbps)", node_id, bitrate_kbps);
	return 0;
}

CO_NMT_reset_cmd_t canopennode_process(uint32_t time_difference_us)
{
	uint32_t timer_next_us = 0;

	if (CO == NULL) {
		return CO_RESET_APP;
	}

	/* v4 统一处理入口: CO_process 内部驱动 SDO/em/NMT/HB/LSS 等对象.
	 * gateway 关闭 (无 CiA 309 需求). */
	return CO_process(CO, false, time_difference_us, &timer_next_us);
}

void canopennode_shutdown(void)
{
	if (CO != NULL) {
		CO_DELETE(CO);
		CO = NULL;
	}
}
