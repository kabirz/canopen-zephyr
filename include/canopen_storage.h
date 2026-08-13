/*
 * Copyright (c) 2026 Kabirz.
 * SPDX-License-Identifier: Apache-2.0
 *
 * CANopen OD 持久化 - Zephyr settings 后端 (v4).
 */

#ifndef CANOPEN_STORAGE_H
#define CANOPEN_STORAGE_H

#include <stdint.h>
#include <stddef.h>

#include "CANopen.h"
#include "storage/CO_storage.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化 CANopen storage (挂 OD 0x1010/0x1011 + settings 后端) */
int canopen_storage_init(CO_storage_t *storage, CO_CANmodule_t *CANmodule,
			 OD_t *OD, CO_storage_entry_t *entries,
			 uint8_t entriesCount, uint32_t *storageInitError);

/* 启动时从 settings 加载持久化 OD 条目 */
int canopen_storage_load(void);

#ifdef __cplusplus
}
#endif

#endif /* CANOPEN_STORAGE_H */
