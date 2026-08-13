/*
 * Copyright (c) 2026 Kabirz.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Zephyr 平台适配头: CANopenNode v4 期望的目标接口.
 *
 * v4 的 301/CO_driver.h 通过 #include "CO_driver_target.h" 拿到:
 *   - 基本类型 (bool_t / float32_t ...)
 *   - CO_CANrx_t / CO_CANtx_t / CO_CANmodule_t 结构
 *   - CO_CANrxMsg_t + CO_CANrxMsg_read* 宏
 *   - CO_LOCK_* / CO_FLAG_* 同步原语
 *
 * 本文件为 Zephyr 完整实现: 锁用 k_mutex, CAN 设备用 Zephyr CAN driver.
 */

#ifndef CO_DRIVER_TARGET_H
#define CO_DRIVER_TARGET_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/drivers/can.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ===== 基本类型 (v4 期望 target 头提供) ===== */

#define CO_LITTLE_ENDIAN
#define CO_SWAP_16(x) (x)
#define CO_SWAP_32(x) (x)
#define CO_SWAP_64(x) (x)

typedef uint_fast8_t bool_t;
typedef float float32_t;
typedef double float64_t;

/* ===== 接收 CAN 帧 (Zephyr can_frame 直转) =====
 * v4 通过 CO_CANrxMsg_t 把帧传给各协议对象. 回调的 message 参数是 void*, 宏内 cast. */

typedef struct can_frame CO_CANrxMsg_t;

#define CO_CANrxMsg_readIdent(msg)  ((uint16_t)(((CO_CANrxMsg_t *)(msg))->id & CAN_STD_ID_MASK))
#define CO_CANrxMsg_readDLC(msg)    ((uint8_t)((CO_CANrxMsg_t *)(msg))->dlc)
#define CO_CANrxMsg_readData(msg)   ((const uint8_t *)((CO_CANrxMsg_t *)(msg))->data)

/* ===== 接收对象 =====
 * 每个 CO_CANrx_t 对应一个 Zephyr CAN RX filter. */

typedef struct {
	uint16_t ident;                    /* CANopen 期望的 11-bit ID */
	uint16_t mask;                     /* 匹配掩码 */
	void *object;                      /* 协议对象 (如 CO_EM_t/CO_NMT_t) */
	void (*CANrx_callback)(void *object, void *message); /* 分发回调 */
	int filter_id;                     /* Zephyr can_add_rx_filter 返回值 */
} CO_CANrx_t;

/* ===== 发送对象 ===== */

typedef struct {
	uint32_t ident;                    /* 11-bit CAN ID */
	uint8_t DLC;
	uint8_t data[8];
	volatile bool_t bufferFull;
	volatile bool_t syncFlag;
} CO_CANtx_t;

/* ===== CAN 模块对象 =====
 * CANptr 指向 Zephyr CAN 设备 (const struct device *) */

typedef struct {
	void *CANptr;                      /* const struct device * */
	CO_CANrx_t *rxArray;
	uint16_t rxSize;
	CO_CANtx_t *txArray;
	uint16_t txSize;
	uint16_t CANerrorStatus;
	volatile bool_t CANnormal;
	volatile bool_t useCANrxFilters;   /* 每个 rx 槽一个硬件 filter, 置 true */
	volatile bool_t bufferInhibitFlag;
	volatile bool_t firstCANtxMessage;
	volatile uint16_t CANtxCount;
	uint32_t errOld;                   /* 上次错误状态 (用于变化检测) */
	bool configured;                   /* CAN module 已初始化 */
} CO_CANmodule_t;

/* ===== OD 存储条目 ===== */

typedef struct {
	void *addr;
	size_t len;
	uint8_t subIndexOD;
	uint8_t attr;
	void *addrNV;
} CO_storage_entry_t;

/* ===== 临界区同步原语 (Zephyr k_mutex, 实现于 src/CO_driver.c) ===== */

void canopen_send_lock(void);
void canopen_send_unlock(void);
void canopen_emcy_lock(void);
void canopen_emcy_unlock(void);
void canopen_od_lock(void);
void canopen_od_unlock(void);

#define CO_LOCK_CAN_SEND(CAN_MODULE)    canopen_send_lock()
#define CO_UNLOCK_CAN_SEND(CAN_MODULE)  canopen_send_unlock()
#define CO_LOCK_EMCY(CAN_MODULE)        canopen_emcy_lock()
#define CO_UNLOCK_EMCY(CAN_MODULE)      canopen_emcy_unlock()
#define CO_LOCK_OD(CAN_MODULE)          canopen_od_lock()
#define CO_UNLOCK_OD(CAN_MODULE)        canopen_od_unlock()

/* ===== CAN 接收与处理线程间的同步标志 (v4 用 rxNew 指针) ===== */

#define CO_MemoryBarrier()
#define CO_FLAG_READ(rxNew)         ((rxNew) != NULL)
#define CO_FLAG_SET(rxNew)          do { CO_MemoryBarrier(); (rxNew) = (void *)1L; } while (0)
#define CO_FLAG_CLEAR(rxNew)        do { CO_MemoryBarrier(); (rxNew) = NULL; } while (0)

#ifdef __cplusplus
}
#endif

#endif /* CO_DRIVER_TARGET_H */
