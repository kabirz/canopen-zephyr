/*
 * Copyright (c) 2026 Kabirz.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Zephyr 应用层 API: 封装 CANopenNode v4 的初始化/处理/销毁.
 *
 * 参考 v4 example/main_blank.c 的流程, 隐藏 CO_new / CO_CANopenInit /
 * CO_process 的复杂参数, 让 sample 的 main.c 保持简洁.
 *
 * 骨架阶段: 实现 OK 但不做真实 CAN 收发 (CO_driver 仍是 stub).
 * 后续会话: CO_driver 完善后这些函数可直接复用.
 */

#ifndef CANOPENNODE_H
#define CANOPENNODE_H

#include <zephyr/kernel.h>
#include <stdbool.h>
#include <stdint.h>

#include "CANopen.h"
#include "OD.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 全局 CO 对象 (单例, 单 OD 模式) */
extern CO_t *CO;

/* Zephyr CANopen 初始化.
 *
 * @param node_id      CANopen 节点 ID (1-127)
 * @param bitrate_kbps CAN 波特率 (kbps, 如 250 表示 250kbps)
 *
 * 流程:
 *   1. 取 devicetree chosen(zephyr_canbus) 设备作为 CANptr
 *   2. CO_new() 分配对象
 *   3. CO_CANopenInit() + CO_CANopenInitPDO() 完成协议栈初始化
 *   4. CO_CANsetNormalMode() 进入正常通信
 *
 * @return 0=成功, -EINVAL=参数错, -ENODEV=CAN 设备不可用,
 *         -EIO=CANopen 初始化失败 (err 输出在 LOG)
 */
int canopennode_init(uint8_t node_id, uint16_t bitrate_kbps);

/* 周期性处理. 在 main 循环或工作队列里以 ~1ms 间隔调用.
 * 处理 SDO server / emergency / NMT / heartbeat / LSS slave.
 *
 * @param time_difference_us 距离上次调用的微秒数
 * @return CO_NMT_reset_cmd_t: CO_RESET_APP / CO_RESET_COMM / CO_RESET_NOT
 */
CO_NMT_reset_cmd_t canopennode_process(uint32_t time_difference_us);

/* 销毁 CO 对象, 释放内存. */
void canopennode_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* CANOPENNODE_H */
