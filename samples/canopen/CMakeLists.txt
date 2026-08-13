# SPDX-License-Identifier: Apache-2.0
#
# CANopen (v4) Zephyr sample - 最小 CANopen 节点.
#
# 用 devicetree chosen(zephyr_canbus) 指向的 CAN 设备 + 默认 OD (v4 example/OD).
# 应用层调 canopennode_init() + 周期调 canopennode_process().
#
# Build:
#   west build -b <board_with_can> samples/canopen
# Run (native_sim):
#   west build -b native_sim samples/canopen
#   build/zephyr/zephyr.exe  (需先 setup vcan + DT 配置 canbus=node)

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(canopen_sample)

target_sources(app PRIVATE
    src/main.c
    objdict/OD.c
)

zephyr_include_directories(objdict)
