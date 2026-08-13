# canopen-zephyr

CANopenNode **v4** 协议栈的 **Zephyr RTOS** 独立模块仓库 (替代 Zephyr 4.4+ 移除的内置 CANopen 集成).

## 为什么需要这个仓库

- **Zephyr 4.4+ 移除了内置 CANopenNode** (`subsys/canbus/Kconfig.canopen` 与 samples 删除), 用户必须自己集成
- 官方 [CANopenNodeZephyr](https://github.com/zephyrproject-rtos/CANopenNodeZephyr) 还停留在 CANopenNode **v1.3** (`stack/` 路径)
- 本仓库直接引入 [CANopenNode](https://github.com/CANopenNode/CANopenNode) **master (v4)** 作为 submodule, 并提供 Zephyr 适配

## 仓库性质

- 自身是 **Zephyr module** (`zephyr/module.yml` 声明, 提供 `CONFIG_CANOPENNODE`)
- 自身是 **Zephyr application** (含 `samples/canopen/`)
- CANopenNode v4 作为 **git submodule** 内嵌 (`CANopenNode/`)
- 通过宿主 workspace 的 manifest (如 `iot-zephyr-app` 仓库的 `apps/west.yml`) 引入, 本仓库不自带 west.yml

## 当前状态: 完整实现

| 部分 | 状态 |
|---|---|
| 仓库结构 (manifest/module/CMake/Kconfig) | ✅ 完成 |
| CANopenNode v4 submodule | ✅ 已添加 (master) |
| `CMakeLists.txt` 编译 v4 源文件 (301/305/extra/...) | ✅ 完成 |
| `include/CO_driver_target.h` (Zephyr 平台头 + k_mutex 锁) | ✅ 完成 |
| `src/CO_driver.c` (Zephyr CAN 收发, RX filter + TX workqueue) | ✅ 完成 |
| `src/canopennode_helpers.c` (v4 初始化序列包装) | ✅ 完成 |
| `src/canopen_storage.c` (Zephyr settings 持久化 OD) | ✅ 完成 |
| `src/canopen_leds.c` (CiA 303-3 LED, GPIO) | ✅ 完成 |
| `src/canopen_sync_thread.c` (1ms SYNC 线程) | ✅ 完成 |
| `samples/canopen/` 完整 sample + 板级 overlay | ✅ 完成 |
| 编译验证 (native_sim) | ✅ 通过 |

CAN 收发已完整实现: RX 走 `can_add_rx_filter` 回调, TX 走 `can_send` + 专用 workqueue 重试, 错误统计映射 Zephyr `can_get_state`.

## 使用

### 在宿主 workspace 中引入本 module

本仓库作为 west project 加入宿主 workspace 的 manifest. 例如 `iot-zephyr-app` 仓库 (`apps/west.yml`) 已添加:

```yaml
projects:
  - name: canopen-zephyr
    remote: kabirz
    revision: main
    path: canopen-zephyr
    submodules:
      - path: CANopenNode
```

然后:

```bash
west update                        # 拉取 canopen-zephyr + CANopenNode submodule
```

### 编译 sample

```bash
# 通用板 (需该板有 CAN 外设 + DT chosen(zephyr_canbus))
west build -b <your_board> canopen-zephyr/samples/canopen

# native_sim (Linux 仿真, 需 vcan + socketcan 配置)
west build -b native_sim canopen-zephyr/samples/canopen \
    -DDTC_OVERLAY_FILE=boards/native_sim.overlay

# STM32F407 示例 (io_edge_f407vet6 等, 需板定义支持 CAN1)
west build -b io_edge_f407vet6 canopen-zephyr/samples/canopen \
    -DDTC_OVERLAY_FILE=boards/stm32f407.overlay
```

> `-DDTC_OVERLAY_FILE` 的相对路径基于 sample 目录 (canopen-zephyr/samples/canopen/boards/)。

### 配置

CANopen 节点 ID 通过 Kconfig:

```
CONFIG_CANOPEN_NODE_ID=10
CONFIG_CANOPENNODE=y
CONFIG_CANOPENNODE_LSS=y           # CiA 305 LSS 支持
CONFIG_CANOPENNODE_SDO_CLIENT=y    # SDO 客户端 (master 功能)
CONFIG_CANOPENNODE_GATEWAY_ASCII=y # CiA 309-3 ASCII 网关
```

CAN 设备由 devicetree chosen 节点指定:

```dts
/ {
    chosen {
        zephyr_canbus = &can1;
    };
};

&can1 {
    status = "okay";
    bitrate = <250000>;
};
```

## 目录结构

```
canopen-zephyr/
├── zephyr/
│   └── module.yml                 声明本仓库为 Zephyr module
├── Kconfig                        CONFIG_CANOPENNODE + 子选项
├── CMakeLists.txt                 编译 v4 源 (301/305/extra/...)
├── CANopenNode/                   git submodule → CANopenNode master (v4)
├── include/
│   ├── CO_driver_target.h         Zephyr 平台头 (骨架)
│   └── canopennode.h              应用层 API
├── src/
│   ├── CO_driver.c                v4 driver Zephyr 适配 (stub)
│   └── canopennode_helpers.c      canopennode_init / process / shutdown 实现
├── samples/canopen/               最小 CANopen 节点 sample
│   ├── CMakeLists.txt
│   ├── prj.conf
│   ├── src/main.c
│   └── objdict/                   v4 example 默认 OD
│       ├── OD.h
│       └── OD.c
├── .gitignore
└── README.md
```

## v4 与 v1.3 主要差异 (背景知识)

| 维度 | v1.3 (CANopenNodeZephyr 旧版) | v4 (本仓库) |
|---|---|---|
| 目录结构 | `stack/CO_*.c` | `301/CO_*.c` + `305/` + `309/` 扁平 |
| OD 接口 | 全局 `OD_RAM_t` 数组 | `OD_t` + `OD_entry_t` iterator |
| 对象创建 | 全局静态 | `CO_new(CO_config_t*)` 动态 |
| 应用层入口 | `CO_init(node_id, bitrate)` | `CO_new()` + `CO_CANopenInit()` 多步 |
| 协议支持 | 301/303/305 | 301/303/**304 (Safety)**/305/309 |

## TODO (可选增强)

1. 板级 overlay 实测 (STM32 真板收发验证)
2. `CONFIG_CANOPENNODE_GATEWAY_ASCII` 启用时接入 UART 命令接口
3. 多 OD (CO_MULTIPLE_OD) 支持
4. `sample.yaml` 的 twister 集成 (CI 自动跑 native_sim)

## 许可证

Apache-2.0 (与 Zephyr 和 CANopenNode 一致)
