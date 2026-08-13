/*
 * Copyright (c) 2026 Kabirz.
 * SPDX-License-Identifier: Apache-2.0
 *
 * CANopenNode v4 driver - Zephyr 完整实现.
 *
 * 实现 v4 期望的 CO_driver.h 全部接口, 底层用 Zephyr CAN driver:
 *   - RX: 每个 CO_CANrx_t 注册一个 can_add_rx_filter(), 回调中做 mask 匹配
 *         并分发到对应协议对象的 CANrx_callback
 *   - TX: can_send() 异步 + 专用 TX workqueue 重试缓冲帧
 *   - 错误: can_get_state() 读 error counters, 映射到 CANerrorStatus
 *   - 锁:  k_mutex (CO_LOCK_* 宏 -> canopen_*_lock 函数)
 *
 * 参考: CANopenNodeZephyr v1.3 的 Zephyr CAN 用法 + v4 example/CO_driver_blank.c
 * 的 v4 接口约定.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/can.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>

#include "301/CO_driver.h"

LOG_MODULE_REGISTER(canopennode_driver, CONFIG_CANOPENNODE_LOG_LEVEL);

/* ================================================================
 * 锁 (CO_LOCK_* -> 这些函数)
 * ================================================================ */

static K_MUTEX_DEFINE(canopen_send_mutex);
static K_MUTEX_DEFINE(canopen_emcy_mutex);
static K_MUTEX_DEFINE(canopen_od_mutex);

void canopen_send_lock(void)
{
	k_mutex_lock(&canopen_send_mutex, K_FOREVER);
}

void canopen_send_unlock(void)
{
	k_mutex_unlock(&canopen_send_mutex);
}

void canopen_emcy_lock(void)
{
	k_mutex_lock(&canopen_emcy_mutex, K_FOREVER);
}

void canopen_emcy_unlock(void)
{
	k_mutex_unlock(&canopen_emcy_mutex);
}

void canopen_od_lock(void)
{
	k_mutex_lock(&canopen_od_mutex, K_FOREVER);
}

void canopen_od_unlock(void)
{
	k_mutex_unlock(&canopen_od_mutex);
}

/* ================================================================
 * TX workqueue (can_send 缓冲帧重试)
 * ================================================================ */

K_KERNEL_STACK_DEFINE(canopen_tx_workq_stack,
		     CONFIG_CANOPENNODE_TX_WORKQUEUE_STACK_SIZE);

struct k_work_q canopen_tx_workq;

struct canopen_tx_work_container {
	struct k_work work;
	CO_CANmodule_t *CANmodule;
};

static struct canopen_tx_work_container canopen_tx_queue;

static void canopen_tx_callback(const struct device *dev, int error, void *arg)
{
	CO_CANmodule_t *CANmodule = arg;

	ARG_UNUSED(dev);

	if (CANmodule == NULL) {
		LOG_ERR("tx callback without CANmodule");
		return;
	}
	if (error == 0) {
		CANmodule->firstCANtxMessage = false;
	}
	/* 缓冲帧可能还在排队, 触发重试 */
	k_work_submit_to_queue(&canopen_tx_workq, &canopen_tx_queue.work);
}

static void canopen_tx_retry(struct k_work *item)
{
	struct canopen_tx_work_container *container =
		CONTAINER_OF(item, struct canopen_tx_work_container, work);
	CO_CANmodule_t *CANmodule = container->CANmodule;
	struct can_frame frame;
	CO_CANtx_t *buffer;
	const struct device *dev;
	int err;
	uint16_t i;

	if (CANmodule == NULL || CANmodule->CANptr == NULL) {
		return;
	}
	dev = (const struct device *)CANmodule->CANptr;

	memset(&frame, 0, sizeof(frame));

	canopen_send_lock();

	for (i = 0; i < CANmodule->txSize; i++) {
		buffer = &CANmodule->txArray[i];

		if (!buffer->bufferFull) {
			continue;
		}

		frame.id = buffer->ident;
		frame.dlc = buffer->DLC;
		memcpy(frame.data, buffer->data, buffer->DLC);

		err = can_send(dev, &frame, K_NO_WAIT, canopen_tx_callback,
			       CANmodule);
		if (err == -EAGAIN) {
			/* 总线忙, 下一轮再试 */
			break;
		} else if (err != 0) {
			LOG_WRN("can_send failed: %d", err);
			/* 保留 bufferFull, 下一轮重试 */
			break;
		}

		buffer->bufferFull = false;
		if (CANmodule->CANtxCount > 0U) {
			CANmodule->CANtxCount--;
		}
	}

	canopen_send_unlock();
}

/* ================================================================
 * RX filter 管理
 * ================================================================ */

static void canopen_detach_all_rx_filters(CO_CANmodule_t *CANmodule)
{
	const struct device *dev;
	uint16_t i;

	if (CANmodule == NULL || CANmodule->CANptr == NULL ||
	    !CANmodule->configured) {
		return;
	}
	dev = (const struct device *)CANmodule->CANptr;

	for (i = 0; i < CANmodule->rxSize; i++) {
		if (CANmodule->rxArray[i].filter_id >= 0) {
			can_remove_rx_filter(dev,
					     CANmodule->rxArray[i].filter_id);
			CANmodule->rxArray[i].filter_id = -ENOSPC;
		}
	}
}

static void canopen_rx_callback(const struct device *dev,
				struct can_frame *frame, void *user_data)
{
	CO_CANmodule_t *CANmodule = (CO_CANmodule_t *)user_data;
	CO_CANrx_t *buffer;
	uint16_t i;

	ARG_UNUSED(dev);

	if (CANmodule == NULL) {
		return;
	}

	/* v4: 使用硬件 filter, 但仍按 rxArray 顺序做 mask 匹配 (zephyr 硬件
	 * filter 可能合并, 回调可能收到非精确匹配帧). 参考 v1.3 的匹配逻辑. */
	for (i = 0; i < CANmodule->rxSize; i++) {
		buffer = &CANmodule->rxArray[i];

		if (buffer->filter_id < 0 || buffer->CANrx_callback == NULL) {
			continue;
		}

		/* v4 blank 的 ident/mask 布局: ident 含 RTR bit (0x0800), mask 含 0x0800 */
		if (((frame->id ^ buffer->ident) & buffer->mask) == 0U) {
			/* 标准 11-bit 帧 */
			buffer->CANrx_callback(buffer->object,
					       (void *)frame);
			break;
		}
	}
}

/* ================================================================
 * v4 接口实现
 * ================================================================ */

void CO_CANsetConfigurationMode(void *CANptr)
{
	const struct device *dev = (const struct device *)CANptr;
	int err;

	if (dev == NULL) {
		return;
	}
	err = can_stop(dev);
	if (err != 0 && err != -EALREADY) {
		LOG_WRN("can_stop failed: %d", err);
	}
}

void CO_CANsetNormalMode(CO_CANmodule_t *CANmodule)
{
	const struct device *dev;
	int err;

	if (CANmodule == NULL || CANmodule->CANptr == NULL) {
		return;
	}
	dev = (const struct device *)CANmodule->CANptr;

	err = can_start(dev);
	if (err != 0 && err != -EALREADY) {
		LOG_ERR("can_start failed: %d", err);
		return;
	}
	CANmodule->CANnormal = true;
}

CO_ReturnError_t CO_CANmodule_init(CO_CANmodule_t *CANmodule, void *CANptr,
				   CO_CANrx_t rxArray[], uint16_t rxSize,
				   CO_CANtx_t txArray[], uint16_t txSize,
				   uint16_t CANbitRate)
{
	const struct device *dev = (const struct device *)CANptr;
	int max_filters;
	int err;
	uint16_t i;

	if (CANmodule == NULL || rxArray == NULL || txArray == NULL ||
	    dev == NULL) {
		LOG_ERR("CO_CANmodule_init: illegal argument");
		return CO_ERROR_ILLEGAL_ARGUMENT;
	}

	if (!device_is_ready(dev)) {
		LOG_ERR("CAN device not ready");
		return CO_ERROR_SYSCALL;
	}

	max_filters = can_get_max_filters(dev, false);
	if (max_filters != -ENOSYS) {
		if (max_filters < 0) {
			LOG_ERR("can_get_max_filters failed: %d", max_filters);
			return CO_ERROR_SYSCALL;
		}
		if (rxSize > max_filters) {
			LOG_ERR("need %d rx filters, only %d available",
				rxSize, max_filters);
			return CO_ERROR_OUT_OF_MEMORY;
		}
	}

	CANmodule->CANptr = (void *)dev;
	CANmodule->rxArray = rxArray;
	CANmodule->rxSize = rxSize;
	CANmodule->txArray = txArray;
	CANmodule->txSize = txSize;
	CANmodule->CANerrorStatus = 0;
	CANmodule->CANnormal = false;
	CANmodule->useCANrxFilters = true;  /* 每槽一个硬件 filter */
	CANmodule->bufferInhibitFlag = false;
	CANmodule->firstCANtxMessage = true;
	CANmodule->CANtxCount = 0;
	CANmodule->errOld = 0;
	CANmodule->configured = false;

	for (i = 0; i < rxSize; i++) {
		rxArray[i].ident = 0U;
		rxArray[i].mask = 0U;
		rxArray[i].object = NULL;
		rxArray[i].CANrx_callback = NULL;
		rxArray[i].filter_id = -ENOSPC;
	}
	for (i = 0; i < txSize; i++) {
		txArray[i].bufferFull = false;
		txArray[i].syncFlag = false;
	}

	err = can_set_bitrate(dev, KHZ(CANbitRate));
	if (err != 0) {
		LOG_ERR("can_set_bitrate(%u) failed: %d", CANbitRate, err);
		return CO_ERROR_ILLEGAL_ARGUMENT;
	}

	err = can_set_mode(dev, CAN_MODE_NORMAL);
	if (err != 0) {
		LOG_ERR("can_set_mode failed: %d", err);
		return CO_ERROR_ILLEGAL_ARGUMENT;
	}

	canopen_tx_queue.CANmodule = CANmodule;
	CANmodule->configured = true;

	LOG_INF("CANopen module init: dev=%p bitrate=%u rx=%u tx=%u",
		dev, CANbitRate, rxSize, txSize);
	return CO_ERROR_NO;
}

void CO_CANmodule_disable(CO_CANmodule_t *CANmodule)
{
	const struct device *dev;
	int err;

	if (CANmodule == NULL || CANmodule->CANptr == NULL) {
		return;
	}
	dev = (const struct device *)CANmodule->CANptr;

	canopen_detach_all_rx_filters(CANmodule);

	err = can_stop(dev);
	if (err != 0 && err != -EALREADY) {
		LOG_WRN("can_stop failed: %d", err);
	}
	CANmodule->CANnormal = false;
}

CO_ReturnError_t CO_CANrxBufferInit(CO_CANmodule_t *CANmodule, uint16_t index,
				    uint16_t ident, uint16_t mask, bool_t rtr,
				    void *object,
				    void (*CANrx_callback)(void *object,
							   void *message))
{
	struct can_filter filter;
	CO_CANrx_t *buffer;
	const struct device *dev;
	int err;

	if (CANmodule == NULL || CANmodule->CANptr == NULL) {
		return CO_ERROR_ILLEGAL_ARGUMENT;
	}
	dev = (const struct device *)CANmodule->CANptr;

	if (object == NULL || CANrx_callback == NULL ||
	    index >= CANmodule->rxSize) {
		LOG_ERR("CO_CANrxBufferInit: illegal argument");
		return CO_ERROR_ILLEGAL_ARGUMENT;
	}

	buffer = &CANmodule->rxArray[index];
	buffer->object = object;
	buffer->CANrx_callback = CANrx_callback;
	buffer->ident = ident & 0x07FFU;
	if (rtr) {
		buffer->ident |= 0x0800U;
	}
	buffer->mask = (mask & 0x07FFU) | 0x0800U;

	/* 配置 Zephyr CAN filter */
	filter.flags = 0U;                    /* 标准帧 */
	filter.id = ident & 0x07FFU;
	filter.mask = mask & 0x07FFU;

	if (buffer->filter_id >= 0) {
		can_remove_rx_filter(dev, buffer->filter_id);
	}

	buffer->filter_id = can_add_rx_filter(dev, canopen_rx_callback,
					      CANmodule, &filter);
	if (buffer->filter_id == -ENOSPC) {
		LOG_ERR("no free CAN rx filter");
		return CO_ERROR_OUT_OF_MEMORY;
	} else if (buffer->filter_id < 0) {
		LOG_ERR("can_add_rx_filter failed: %d", buffer->filter_id);
		buffer->filter_id = -ENOSPC;
		return CO_ERROR_SYSCALL;
	}

	(void)err;
	return CO_ERROR_NO;
}

CO_CANtx_t *CO_CANtxBufferInit(CO_CANmodule_t *CANmodule, uint16_t index,
			       uint16_t ident, bool_t rtr, uint8_t noOfBytes,
			       bool_t syncFlag)
{
	CO_CANtx_t *buffer;

	if (CANmodule == NULL || index >= CANmodule->txSize) {
		LOG_ERR("CO_CANtxBufferInit: illegal argument");
		return NULL;
	}

	buffer = &CANmodule->txArray[index];
	buffer->ident = ident & 0x07FFU;
	buffer->DLC = noOfBytes;
	buffer->bufferFull = false;
	buffer->syncFlag = syncFlag;
	(void)rtr;  /* CANopen 不用 RTR */

	return buffer;
}

CO_ReturnError_t CO_CANsend(CO_CANmodule_t *CANmodule, CO_CANtx_t *buffer)
{
	CO_ReturnError_t ret = CO_ERROR_NO;
	const struct device *dev;
	struct can_frame frame;
	int err;

	if (CANmodule == NULL || buffer == NULL ||
	    CANmodule->CANptr == NULL) {
		return CO_ERROR_ILLEGAL_ARGUMENT;
	}
	dev = (const struct device *)CANmodule->CANptr;

	memset(&frame, 0, sizeof(frame));

	canopen_send_lock();

	/* 检测溢出 */
	if (buffer->bufferFull) {
		if (!CANmodule->firstCANtxMessage) {
			CANmodule->CANerrorStatus |= CO_CAN_ERRTX_OVERFLOW;
		}
		buffer->bufferFull = false;
		ret = CO_ERROR_TX_OVERFLOW;
	}

	frame.id = buffer->ident;
	frame.dlc = buffer->DLC;
	memcpy(frame.data, buffer->data, buffer->DLC);

	err = can_send(dev, &frame, K_NO_WAIT, canopen_tx_callback, CANmodule);
	if (err == -EAGAIN) {
		/* 总线忙, 入队等待 workqueue 重试 */
		buffer->bufferFull = true;
		CANmodule->CANtxCount++;
	} else if (err != 0) {
		LOG_WRN("can_send failed: %d", err);
		CANmodule->CANerrorStatus |= CO_CAN_ERRTX_BUS_OFF;
		ret = CO_ERROR_TX_UNCONFIGURED;
	}

	canopen_send_unlock();

	return ret;
}

void CO_CANclearPendingSyncPDOs(CO_CANmodule_t *CANmodule)
{
	CO_CANtx_t *buffer;
	uint16_t i;
	bool_t tpdoDeleted = false;

	if (CANmodule == NULL) {
		return;
	}

	canopen_send_lock();

	for (i = 0; i < CANmodule->txSize; i++) {
		buffer = &CANmodule->txArray[i];
		if (buffer->bufferFull && buffer->syncFlag) {
			buffer->bufferFull = false;
			if (CANmodule->CANtxCount > 0U) {
				CANmodule->CANtxCount--;
			}
			tpdoDeleted = true;
		}
	}

	canopen_send_unlock();

	if (tpdoDeleted) {
		CANmodule->CANerrorStatus |= CO_CAN_ERRTX_PDO_LATE;
	}
}

/* 用 Zephyr can_get_state 读错误计数, 映射到 CANerrorStatus (v4 blank 逻辑) */
void CO_CANmodule_process(CO_CANmodule_t *CANmodule)
{
	const struct device *dev;
	struct can_bus_err_cnt err_cnt;
	enum can_state state;
	uint16_t rxErrors, txErrors, overflow;
	uint32_t err;
	uint16_t status;
	int rc;

	if (CANmodule == NULL || CANmodule->CANptr == NULL) {
		return;
	}
	dev = (const struct device *)CANmodule->CANptr;

	rc = can_get_state(dev, &state, &err_cnt);
	if (rc != 0) {
		LOG_WRN("can_get_state failed: %d", rc);
		return;
	}

	/* Zephyr 无 mailbox overflow 计数 API, 置 0 (参考 v1.3 做法) */
	overflow = 0U;
	rxErrors = err_cnt.rx_err_cnt;
	txErrors = err_cnt.tx_err_cnt;

	err = ((uint32_t)txErrors << 16) | ((uint32_t)rxErrors << 8) | overflow;

	if (CANmodule->errOld == err) {
		return;
	}
	CANmodule->errOld = err;
	status = CANmodule->CANerrorStatus;

	if (state == CAN_STATE_BUS_OFF) {
		status |= CO_CAN_ERRTX_BUS_OFF;
	} else {
		status &= (uint16_t)~(CO_CAN_ERRTX_BUS_OFF | CO_CAN_ERRRX_WARNING |
				      CO_CAN_ERRRX_PASSIVE | CO_CAN_ERRTX_WARNING |
				      CO_CAN_ERRTX_PASSIVE);

		if (rxErrors >= 128U) {
			status |= CO_CAN_ERRRX_WARNING | CO_CAN_ERRRX_PASSIVE;
		} else if (rxErrors >= 96U) {
			status |= CO_CAN_ERRRX_WARNING;
		}

		if (txErrors >= 128U) {
			status |= CO_CAN_ERRTX_WARNING | CO_CAN_ERRTX_PASSIVE;
		} else if (txErrors >= 96U) {
			status |= CO_CAN_ERRTX_WARNING;
		}

		if ((status & CO_CAN_ERRTX_PASSIVE) == 0U) {
			status &= (uint16_t)~CO_CAN_ERRTX_OVERFLOW;
		}
	}

	if (overflow != 0U) {
		status |= CO_CAN_ERRRX_OVERFLOW;
	}

	CANmodule->CANerrorStatus = status;
}

/* Zephyr 的 RX 由 filter 回调驱动, 无硬件中断需处理. 空实现满足接口. */
void CO_CANinterrupt(CO_CANmodule_t *CANmodule)
{
	ARG_UNUSED(CANmodule);
}

/* ================================================================
 * 初始化: 启动 TX workqueue
 * ================================================================ */

static int canopennode_driver_init(void)
{
	k_work_queue_start(&canopen_tx_workq, canopen_tx_workq_stack,
			   K_KERNEL_STACK_SIZEOF(canopen_tx_workq_stack),
			   CONFIG_CANOPENNODE_TX_WORKQUEUE_PRIORITY, NULL);
	k_thread_name_set(&canopen_tx_workq.thread, "canopen_tx");

	k_work_init(&canopen_tx_queue.work, canopen_tx_retry);

	return 0;
}

SYS_INIT(canopennode_driver_init, APPLICATION, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);
