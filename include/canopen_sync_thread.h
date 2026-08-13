/*
 * Copyright (c) 2026 Kabirz.
 * SPDX-License-Identifier: Apache-2.0
 *
 * CANopen SYNC 线程 (v4).
 */

#ifndef CANOPEN_SYNC_THREAD_H
#define CANOPEN_SYNC_THREAD_H

#include "CANopen.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 启动 SYNC 线程 (CO 初始化完成后调用) */
int canopen_sync_start(CO_t *co);

#ifdef __cplusplus
}
#endif

#endif /* CANOPEN_SYNC_THREAD_H */
