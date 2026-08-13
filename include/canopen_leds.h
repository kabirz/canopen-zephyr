/*
 * Copyright (c) 2026 Kabirz.
 * SPDX-License-Identifier: Apache-2.0
 *
 * CANopen LED 指示 (CiA 303-3) - Zephyr GPIO (v4).
 */

#ifndef CANOPEN_LEDS_H
#define CANOPEN_LEDS_H

#include "303/CO_LEDs.h"
#include "301/CO_Emergency.h"
#include "301/CO_NMT_Heartbeat.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化 LED GPIO + CO_LEDs 对象 */
int canopen_leds_init(CO_LEDs_t *LEDs);

/* 周期处理 LED 状态机 + 驱动 GPIO (main 循环调用) */
void canopen_leds_process(CO_LEDs_t *LEDs, uint32_t time_difference_us,
			  CO_NMT_internalState_t NMTstate, CO_EM_t *em);

#ifdef __cplusplus
}
#endif

#endif /* CANOPEN_LEDS_H */
