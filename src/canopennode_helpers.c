/*
 * Copyright (c) 2026 Kabirz.
 * SPDX-License-Identifier: Apache-2.0
 *
 * canopennode.h 应用层 API 实现 (Zephyr v4 包装).
 *
 * 参考 v4 example/main_blank.c 的初始化序列:
 *   CO_new() -> CO_CANinit() -> CO_CANopenInit() -> CO_LSSinit()
 *   -> CO_CANopenInitPDO() -> CO_CANsetNormalMode()
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

/* 默认 node ID / 波特率 (可在 reset 间被 LSS 修改) */
static uint8_t pending_node_id;
static uint16_t pending_bitrate;

/* CAN 设备: devicetree chosen(zephyr_canbus) */
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

	void *CANptr = (void *)CAN_NODE;
	pending_node_id = node_id;
	pending_bitrate = bitrate_kbps;

	/* [1/4] CAN 模块初始化 (含 bitrate 配置, CO_CANmodule_init 内部) */
	CO->CANmodule->CANnormal = false;
	err = CO_CANinit(CO, CANptr, pending_bitrate);
	if (err != CO_ERROR_NO) {
		LOG_ERR("CO_CANinit 失败: %d", err);
		CO_delete(CO);
		return -EIO;
	}

	/* [2/4] CANopen 核心对象 (NMT/SDO/EM/HB/...)
	 * 返回 CO_ERROR_NODE_ID_UNCONFIGURED_LSS 表示 LSS 待配置, 可接受 */
	err = CO_CANopenInit(CO, CO->NMT, CO->em, OD, OD_STATUS_BITS, NMT_CONTROL,
			     FIRST_HB_TIME_MS, SDO_SRV_TIMEOUT_TIME_MS,
			     SDO_CLI_TIMEOUT_TIME_MS, SDO_CLI_BLOCK_TRANSFER,
			     pending_node_id, &err_info);
	if (err != CO_ERROR_NO && err != CO_ERROR_NODE_ID_UNCONFIGURED_LSS) {
		LOG_ERR("CO_CANopenInit 失败: %d (err_info=%u)", err, err_info);
		CO_delete(CO);
		return -EIO;
	}

#if ((CO_CONFIG_LSS) & CO_CONFIG_LSS_SLAVE) != 0
	/* [3/4] LSS slave (CiA 305): 用 OD 0x1018 identity */
	{
		CO_LSS_address_t lss_address = {
			.identity = {
				.vendorID = OD_PERSIST_COMM.x1018_identity.vendor_ID,
				.productCode = OD_PERSIST_COMM.x1018_identity.productCode,
				.revisionNumber = OD_PERSIST_COMM.x1018_identity.revisionNumber,
				.serialNumber = OD_PERSIST_COMM.x1018_identity.serialNumber,
			},
		};

		err = CO_LSSinit(CO, &lss_address, &pending_node_id, &pending_bitrate);
		if (err != CO_ERROR_NO) {
			LOG_ERR("CO_LSSinit 失败: %d", err);
			CO_delete(CO);
			return -EIO;
		}
	}
#endif /* LSS */

	/* [4/4] PDO 映射初始化 */
	err = CO_CANopenInitPDO(CO, CO->em, OD, pending_node_id, &err_info);
	if (err != CO_ERROR_NO) {
		LOG_ERR("CO_CANopenInitPDO 失败: %d (err_info=%u)", err, err_info);
		CO_delete(CO);
		return -EIO;
	}

	/* 进入正常通信 */
	CO_CANsetNormalMode(CO->CANmodule);

	LOG_INF("CANopen 初始化完成 (node_id=%u, bitrate=%u kbps)",
		node_id, bitrate_kbps);
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
		CO_delete(CO);
		CO = NULL;
	}
}
