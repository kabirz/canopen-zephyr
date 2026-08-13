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
- 通过 `west.yml` 仅引入 Zephyr 本体 (其他 module 由用户自己加)

## 当前状态: 骨架阶段

| 部分 | 状态 |
|---|---|
| 仓库结构 (manifest/module/CMake/Kconfig) | ✅ 完成 |
| CANopenNode v4 submodule | ✅ 已添加 (master) |
| `CMakeLists.txt` 编译 v4 源文件 (301/305/extra/...) | ✅ 完成 |
| `include/CO_driver_target.h` (Zephyr 平台头) | 🟡 骨架 (用 v4 example, 单线程无锁) |
| `src/CO_driver.c` (Zephyr CAN 适配) | 🟡 stub (函数全实现, 不连真实 CAN) |
| `include/canopennode.h` 应用层 API 包装 | ✅ 完成 |
| `samples/canopen/` 最小 sample | ✅ 完成 |
| **CAN 真实收发** | ❌ TODO (后续会话) |

骨架阶段目标: **module 能编译通过, sample 能 build 出二进制**. CO_driver 是 stub, 不连真实 CAN 硬件.

## 使用

### 准备 workspace

```bash
west init -l canopen-zephyr        # 用本仓库 manifest 初始化 workspace
west update                        # 拉取 zephyr + CANopenNode submodule
```

### 编译 sample

```bash
# 通用板 (需该板有 CAN 外设 + DT chosen(zephyr_canbus))
west build -b <your_board> samples/canopen

# native_sim (Linux 仿真, 需配置 vcan + DT 关联)
west build -b native_sim samples/canopen
```

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
├── west.yml                       manifest: import zephyr
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

## TODO (后续会话)

1. `src/CO_driver.c`: 用 Zephyr `can_send()` / `can_add_rx_filter()` 实现真实 CAN 收发
2. `include/CO_driver_target.h`: 用 `k_spinlock` / `k_mutex` 替换 stub 锁
3. 加 `src/canopen_storage.c`: Zephyr settings 后端持久化 OD
4. 加 `src/canopen_leds.c`: GPIO LED 跟随 CANopen 状态 (CiA 303-3)
5. 加 `src/canopen_sync_thread.c`: 内部 SYNC 线程 (独立于 main 循环)
6. 加板级 overlay 示例 (STM32 / native_sim vcan)
7. 加 `sample.yaml` 用于 twister / CI

## 许可证

Apache-2.0 (与 Zephyr 和 CANopenNode 一致)
