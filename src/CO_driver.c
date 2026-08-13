/*
 * Copyright (c) 2026 Kabirz.
 * SPDX-License-Identifier: Apache-2.0
 *
 * CANopenNode v4 driver - Zephyr 适配 (骨架版本).
 *
 * 本文件是 v4 期望的 CO_driver.c 平台实现. 骨架阶段:
 *   - 所有函数按 v4 example/CO_driver_blank.c 的 stub 形式实现
 *   - 返回成功状态, 保证 module 编译通过
 *   - 不连接真实 Zephyr CAN 设备 (后续会话替换为 can_send/can_add_rx_filter)
 *
 * v4 期望的 11 个接口函数 (301/CO_driver.h 声明):
 *   CO_CANsetConfigurationMode / CO_CANsetNormalMode
 *   CO_CANmodule_init / CO_CANmodule_disable
 *   CO_CANrxBufferInit / CO_CANtxBufferInit
 *   CO_CANsend / CO_CANclearPendingSyncPDOs
 *   CO_CANmodule_process / CO_CANinterrupt
 *
 * 后续会话将基于本骨架补完整 Zephyr 实现:
 *   - CANptr -> const struct device * (DEVICE_DT_GET(zephyr_canbus))
 *   - CO_CANsend: 用 Zephyr can_send() + TX workqueue
 *   - CO_CANrxBufferInit: 用 can_add_rx_filter() + 回调路由
 *   - 错误统计: 用 CAN_STATS / can_get_state()
 */

#include <stdlib.h>
#include <string.h>

#include "301/CO_driver.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(canopennode_driver, CONFIG_CANOPENNODE_LOG_LEVEL);

/* 把 CANptr 放到配置模式 (bitrate/node-id 可改).
 * 骨架: Zephyr CAN 用 can_stop() 进入该状态, 此处暂空. */
void CO_CANsetConfigurationMode(void *CANptr)
{
	ARG_UNUSED(CANptr);
}

void CO_CANsetNormalMode(CO_CANmodule_t *CANmodule)
{
	if (CANmodule == NULL) {
		return;
	}
	/* 骨架: 直接置 CANnormal, 后续会调 can_start() */
	CANmodule->CANnormal = true;
}

CO_ReturnError_t CO_CANmodule_init(CO_CANmodule_t *CANmodule, void *CANptr,
				   CO_CANrx_t rxArray[], uint16_t rxSize,
				   CO_CANtx_t txArray[], uint16_t txSize,
				   uint16_t CANbitRate)
{
	uint16_t i;

	if (CANmodule == NULL || rxArray == NULL || txArray == NULL) {
		return CO_ERROR_ILLEGAL_ARGUMENT;
	}

	CANmodule->CANptr = CANptr;
	CANmodule->rxArray = rxArray;
	CANmodule->rxSize = rxSize;
	CANmodule->txArray = txArray;
	CANmodule->txSize = txSize;
	CANmodule->CANerrorStatus = 0;
	CANmodule->CANnormal = false;
	CANmodule->useCANrxFilters = false;
	CANmodule->bufferInhibitFlag = false;
	CANmodule->firstCANtxMessage = true;
	CANmodule->CANtxCount = 0;
	CANmodule->errOld = 0;

	for (i = 0; i < rxSize; i++) {
		rxArray[i].ident = 0;
		rxArray[i].mask = 0;
		rxArray[i].object = NULL;
		rxArray[i].CANrx_callback = NULL;
	}
	for (i = 0; i < txSize; i++) {
		txArray[i].bufferFull = false;
		txArray[i].syncFlag = false;
	}

	/* 骨架: 不调用 Zephyr CAN API.
	 * 后续: 用 device_is_ready(CANptr) + can_start() + bitrate 配置. */
	LOG_DBG("module init (stub): CANptr=%p, bitrate=%u, rxSize=%u, txSize=%u",
		CANptr, CANbitRate, rxSize, txSize);

	return CO_ERROR_NO;
}

void CO_CANmodule_disable(CO_CANmodule_t *CANmodule)
{
	if (CANmodule == NULL) {
		return;
	}
	/* 骨架: 后续调 can_stop() + 移除 RX filter */
	CANmodule->CANnormal = false;
}

CO_ReturnError_t CO_CANrxBufferInit(CO_CANmodule_t *CANmodule, uint16_t index,
				    uint16_t ident, uint16_t mask, bool_t rtr,
				    void *object,
				    void (*CANrx_callback)(void *object, void *message))
{
	if (CANmodule == NULL || index >= CANmodule->rxSize) {
		return CO_ERROR_ILLEGAL_ARGUMENT;
	}

	/* 骨架: 仅记参数, 后续调 can_add_rx_filter() 注册匹配规则 */
	CO_CANrx_t *rx = &CANmodule->rxArray[index];
	rx->ident = ident;
	rx->mask = mask;
	rx->object = object;
	rx->CANrx_callback = CANrx_callback;
	ARG_UNUSED(rtr);

	return CO_ERROR_NO;
}

CO_CANtx_t *CO_CANtxBufferInit(CO_CANmodule_t *CANmodule, uint16_t index,
			       uint32_t ident, bool_t rtr, uint8_t noOfBytes,
			       bool_t syncFlag)
{
	if (CANmodule == NULL || index >= CANmodule->txSize || noOfBytes > 8) {
		return NULL;
	}

	/* 骨架: 仅填 buffer 头, 后续真实发送在 CO_CANsend */
	CO_CANtx_t *tx = &CANmodule->txArray[index];
	tx->ident = ident;
	tx->DLC = noOfBytes;
	tx->bufferFull = false;
	tx->syncFlag = syncFlag;
	ARG_UNUSED(rtr);

	return tx;
}

CO_ReturnError_t CO_CANsend(CO_CANmodule_t *CANmodule, CO_CANtx_t *buffer)
{
	if (CANmodule == NULL || buffer == NULL) {
		return CO_ERROR_ILLEGAL_ARGUMENT;
	}
	if (buffer->bufferFull) {
		/* 已有待发, 报 bus-off 风险 */
		CANmodule->CANerrorStatus |= CO_CAN_ERRTX_BUSOFF;
		return CO_ERROR_TX_BUSY;
	}

	/* 骨架: 不真实发送. 后续用 can_send(CANptr, &frame, K_NO_WAIT, cb, NULL).
	 * 现阶段仅置 bufferFull 模拟异步流程, 立即清空. */
	buffer->bufferFull = true;
	CANmodule->CANtxCount++;
	buffer->bufferFull = false;
	CANmodule->CANtxCount--;

	return CO_ERROR_NO;
}

void CO_CANclearPendingSyncPDOs(CO_CANmodule_t *CANmodule)
{
	if (CANmodule == NULL) {
		return;
	}
	for (uint16_t i = 0; i < CANmodule->txSize; i++) {
		CANmodule->txArray[i].bufferFull = false;
	}
}

/* 错误统计 (骨架用静态变量, 后续从 Zephyr CAN_STATS 取) */
static uint16_t rxErrors = 0, txErrors = 0, overflow = 0;

void CO_CANmodule_process(CO_CANmodule_t *CANmodule)
{
	if (CANmodule == NULL) {
		return;
	}

	/* 骨架: 复制 v4 example 的错误状态计算逻辑. 后续接入真实 Zephyr
	 * CAN state (can_get_state + state_transition callback). */
	uint32_t err = ((uint32_t)rxErrors << 16) | txErrors;
	if (CANmodule->errOld != err) {
		uint16_t status = CANmodule->CANerrorStatus;

		if (rxErrors >= 256U || txErrors >= 256U) {
			status |= CO_CAN_ERRTX_BUSOFF | CO_CAN_ERRRX_BUSOFF;
		} else {
			status &= ~(CO_CAN_ERRTX_BUSOFF | CO_CAN_ERRRX_BUSOFF);
		}
		if (rxErrors >= 96U) {
			status |= CO_CAN_ERRRX_WARN_PASSIVE;
		} else {
			status &= ~CO_CAN_ERRRX_WARN_PASSIVE;
		}
		if (txErrors >= 96U) {
			status |= CO_CAN_ERRTX_WARN_PASSIVE;
		} else {
			status &= ~CO_CAN_ERRTX_WARN_PASSIVE;
		}
		if (overflow > 0U) {
			status |= CO_CAN_ERRRX_OVERFLOW;
		}
		CANmodule->CANerrorStatus = status;
		CANmodule->errOld = err;
	}
}

void CO_CANinterrupt(CO_CANmodule_t *CANmodule)
{
	/* 骨架: Zephyr 模型下 CAN RX 由内核 CAN driver 回调驱动, 无需此函数.
	 * 保留空实现以满足 v4 接口. */
	ARG_UNUSED(CANmodule);
}
