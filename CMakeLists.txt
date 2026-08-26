# SPDX-License-Identifier: Apache-2.0
#
# Zephyr module CMake for CANopenNode v4 integration.
# v4 source layout (flat per CiA profile, not v1.3's stack/ subdir):
#   301/  - application layer (always built)
#   303/  - LEDs (optional, CANOPENNODE_LEDS)
#   305/  - LSS (optional, CANOPENNODE_LSS)
#   309/  - gateway (optional, CANOPENNODE_GATEWAY_ASCII)
#   storage/  - persistence (optional, CANOPENNODE_STORAGE)
#   extra/    - trace (always built)
#   CANopen.c - top-level init (always built)

if(CONFIG_CANOPENNODE)
	zephyr_library()

	zephyr_include_directories(
		${CMAKE_CURRENT_SOURCE_DIR}/CANopenNode
		${CMAKE_CURRENT_SOURCE_DIR}/CANopenNode/301
		${CMAKE_CURRENT_SOURCE_DIR}/CANopenNode/303
		${CMAKE_CURRENT_SOURCE_DIR}/CANopenNode/305
		${CMAKE_CURRENT_SOURCE_DIR}/CANopenNode/309
		${CMAKE_CURRENT_SOURCE_DIR}/CANopenNode/storage
		${CMAKE_CURRENT_SOURCE_DIR}/CANopenNode/extra
		${CMAKE_CURRENT_SOURCE_DIR}/include
	)

	zephyr_library_sources(
		# v4 顶层 + 301 (核心协议, 必选)
		CANopenNode/CANopen.c
		CANopenNode/301/CO_Emergency.c
		CANopenNode/301/CO_HBconsumer.c
		CANopenNode/301/CO_NMT_Heartbeat.c
		CANopenNode/301/CO_ODinterface.c
		CANopenNode/301/CO_PDO.c
		CANopenNode/301/CO_SDOserver.c
		CANopenNode/301/CO_SYNC.c
		CANopenNode/301/CO_TIME.c
		CANopenNode/301/CO_fifo.c
		CANopenNode/301/crc16-ccitt.c

		# extra (trace)
		CANopenNode/extra/CO_trace.c

		# Zephyr 平台适配 (driver stub, 后续会话补完整)
		src/CO_driver.c
	)

	# Node guarding (CiA 301 v4 新增, 可选)
	zephyr_library_sources_ifdef(CONFIG_CANOPENNODE_NODE_GUARDING
		CANopenNode/301/CO_Node_Guarding.c)

	# SDO client (master)
	zephyr_library_sources_ifdef(CONFIG_CANOPENNODE_SDO_CLIENT
		CANopenNode/301/CO_SDOclient.c)

	# LSS (CiA 305)
	zephyr_library_sources_ifdef(CONFIG_CANOPENNODE_LSS
		CANopenNode/305/CO_LSSmaster.c
		CANopenNode/305/CO_LSSslave.c)

	# LEDs (CiA 303-3)
	zephyr_library_sources_ifdef(CONFIG_CANOPENNODE_LEDS
		CANopenNode/303/CO_LEDs.c)

	# CiA 309-3 ASCII gateway
	zephyr_library_sources_ifdef(CONFIG_CANOPENNODE_GATEWAY_ASCII
		CANopenNode/309/CO_gateway_ascii.c)

	# 持久化存储 (Zephyr settings 后端, 不需要 CO_storageEeprom.c 的 EEPROM 实现)
	zephyr_library_sources_ifdef(CONFIG_CANOPENNODE_STORAGE
		CANopenNode/storage/CO_storage.c
		src/canopen_storage.c)

	# LED 指示 (CiA 303-3, Zephyr GPIO)
	zephyr_library_sources_ifdef(CONFIG_CANOPENNODE_LEDS
		src/canopen_leds.c)

	# SYNC 线程
	zephyr_library_sources_ifdef(CONFIG_CANOPENNODE_SYNC_THREAD
		src/canopen_sync_thread.c)

	# 应用层便利封装 (canopennode.h)
	zephyr_library_sources(
		src/canopennode_helpers.c
	)

	# Kconfig -> CANopenNode 编译配置宏映射 (CO_config.h / CO_SDOserver.h).
	# 不映射时栈用自身默认: SDO buffer 32 字节, SDO 块传输不编译, LSS 宏
	# 恒为 slave-enabled (源码未编译时链接失败).
	zephyr_library_compile_definitions(
		CO_CONFIG_SDO_SRV_BUFFER_SIZE=${CONFIG_CANOPENNODE_SDO_BUFFER_SIZE}
	)
	if(CONFIG_CANOPENNODE_SDO_BLOCK)
		zephyr_library_compile_definitions(
			CO_CONFIG_SDO_SRV=\(CO_CONFIG_SDO_SRV_SEGMENTED\|CO_CONFIG_SDO_SRV_BLOCK\|CO_CONFIG_GLOBAL_FLAG_CALLBACK_PRE\|CO_CONFIG_GLOBAL_FLAG_TIMERNEXT\|CO_CONFIG_GLOBAL_FLAG_OD_DYNAMIC\)
		)
		# 块传输 CRC 校验需要 CRC16 (CO_SDOserver.c 配置检查要求 CO_CONFIG_CRC16 含 ENABLE)
		zephyr_library_compile_definitions(
			CO_CONFIG_CRC16=\(CO_CONFIG_CRC16_ENABLE\)
		)
	endif()
	if(NOT CONFIG_CANOPENNODE_LSS)
		# LSS 源码未编译时必须同时清零 CO_CONFIG_LSS, 否则 CO_new 引用未链接对象
		zephyr_library_compile_definitions(CO_CONFIG_LSS=0)
	endif()
endif()
