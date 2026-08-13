/*
 * Copyright (c) 2026 Kabirz.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Zephyr 平台适配头: CANopenNode v4 期望的目标接口.
 *
 * v4 的 301/CO_driver.h 通过 #include "CO_driver_target.h" 拿到:
 *   - 基本类型 (bool_t / float32_t ...)
 *   - CO_CANrx_t / CO_CANtx_t / CO_CANmodule_t 结构
 *   - CO_LOCK_* / CO_FLAG_* 同步原语
 *
 * 本文件是骨架版本: 沿用 CANopenNode example 的最简定义, 仅保证编译通过.
 * 后续会话将替换为真实 Zephyr 同步原语 (k_spinlock / k_mutex) + CAN 驱动.
 */

#ifndef CO_DRIVER_TARGET_H
#define CO_DRIVER_TARGET_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/kernel.h>

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

/* ===== CAN 接收帧读取宏 (骨架, 后续从 Zephyr can_frame 转换) ===== */

#define CO_CANrxMsg_readIdent(msg) ((uint16_t)0)
#define CO_CANrxMsg_readDLC(msg)   ((uint8_t)0)
#define CO_CANrxMsg_readData(msg)  ((const uint8_t *)NULL)

/* ===== 接收/发送/模块对象 (与 v4 example 一致, 后续会扩展 Zephyr CAN 句柄) ===== */

typedef struct {
	uint16_t ident;
	uint16_t mask;
	void *object;
	void (*CANrx_callback)(void *object, void *message);
} CO_CANrx_t;

typedef struct {
	uint32_t ident;
	uint8_t DLC;
	uint8_t data[8];
	volatile bool_t bufferFull;
	volatile bool_t syncFlag;
} CO_CANtx_t;

typedef struct {
	void *CANptr;             /* Zephyr CAN 设备 (const struct device *) */
	CO_CANrx_t *rxArray;
	uint16_t rxSize;
	CO_CANtx_t *txArray;
	uint16_t txSize;
	uint16_t CANerrorStatus;
	volatile bool_t CANnormal;
	volatile bool_t useCANrxFilters;
	volatile bool_t bufferInhibitFlag;
	volatile bool_t firstCANtxMessage;
	volatile uint16_t CANtxCount;
	uint32_t errOld;
	/* Zephyr 适配后续增加: CAN filter id 数组 / TX workqueue 等 */
} CO_CANmodule_t;

/* ===== OD 存储条目 ===== */

typedef struct {
	void *addr;
	size_t len;
	uint8_t subIndexOD;
	uint8_t attr;
	void *addrNV;
} CO_storage_entry_t;

/* ===== 临界区同步原语 =====
 * 骨架阶段: 空实现 (单线程安全). 后续替换为:
 *   - CO_LOCK_CAN_SEND: k_spinlock (TX 路径)
 *   - CO_LOCK_EMCY:     irq_lock
 *   - CO_LOCK_OD:       k_mutex (OD 跨线程访问) */

#define CO_LOCK_CAN_SEND(CAN_MODULE)
#define CO_UNLOCK_CAN_SEND(CAN_MODULE)

#define CO_LOCK_EMCY(CAN_MODULE)
#define CO_UNLOCK_EMCY(CAN_MODULE)

#define CO_LOCK_OD(CAN_MODULE)
#define CO_UNLOCK_OD(CAN_MODULE)

/* ===== CAN 接收与处理线程间的同步标志 =====
 * v4 用 rxNew 指针作为标志, CO_FLAG_SET/CLEAR 控制其值. */

#define CO_MemoryBarrier()
#define CO_FLAG_READ(rxNew)         ((rxNew) != NULL)
#define CO_FLAG_SET(rxNew)          do { CO_MemoryBarrier(); (rxNew) = (void *)1L; } while (0)
#define CO_FLAG_CLEAR(rxNew)        do { CO_MemoryBarrier(); (rxNew) = NULL; } while (0)

#ifdef __cplusplus
}
#endif

#endif /* CO_DRIVER_TARGET_H */
