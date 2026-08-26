/*
 * Copyright (c) 2026 Kabirz.
 * SPDX-License-Identifier: Apache-2.0
 *
 * CANopen Object Dictionary 持久化 - Zephyr settings 后端 (v4).
 *
 * 实现 v4 example/CO_storageBlank.c 的 store/restore 回调模式:
 *   - store:   把 CO_storage_entry_t 指向的 OD 数据写入 Zephyr settings
 *   - restore: 从 Zephyr settings 读回覆盖 OD (或擦除恢复默认)
 *
 * 通过 OD 0x1010 (Store parameters) / 0x1011 (Restore default) 触发,
 * 由 CO_storage_init() 挂到 OD. 应用侧调 canopen_storage_init() 完成.
 *
 * settings key 前缀: "canopen/od/<subIndexOD>".
 */

#if defined(CONFIG_CANOPENNODE_STORAGE)

#include <string.h>

#include <zephyr/settings/settings.h>
#include <zephyr/logging/log.h>

#include "storage/CO_storage.h"
#include "301/CO_driver.h"

LOG_MODULE_REGISTER(canopennode_storage, CONFIG_CANOPENNODE_LOG_LEVEL);

#define STORAGE_KEY_PREFIX "canopen/od"

/* OD 0x1010 store magic ('s','a','v','e') - 由 CO_storage 内部处理,
 * 这里只提供 Zephyr settings 的读/写后端. */

static ODR_t canopen_storage_store(CO_storage_entry_t *entry,
				   CO_CANmodule_t *CANmodule)
{
	char key[16];
	int err;

	ARG_UNUSED(CANmodule);

	if (entry == NULL || entry->addr == NULL || entry->len == 0) {
		return ODR_HW;
	}

	snprintk(key, sizeof(key), "%s/%u", STORAGE_KEY_PREFIX,
		 entry->subIndexOD);

	err = settings_save_one(key, entry->addr, entry->len);
	if (err != 0) {
		LOG_ERR("settings_save_one(%s) failed: %d", key, err);
		return ODR_HW;
	}
	LOG_DBG("stored OD entry subIndex=%u len=%u", entry->subIndexOD,
		entry->len);
	return ODR_OK;
}

static ODR_t canopen_storage_restore(CO_storage_entry_t *entry,
				     CO_CANmodule_t *CANmodule)
{
	char key[16];
	int err;

	ARG_UNUSED(CANmodule);

	if (entry == NULL || entry->addr == NULL || entry->len == 0) {
		return ODR_HW;
	}

	snprintk(key, sizeof(key), "%s/%u", STORAGE_KEY_PREFIX,
		 entry->subIndexOD);

	/* 从 settings 读回覆盖 OD */
	err = settings_load_subtree(key);
	if (err != 0 && err != -ENOENT) {
		LOG_ERR("settings_load_subtree(%s) failed: %d", key, err);
		return ODR_HW;
	}
	if (err == -ENOENT) {
		LOG_DBG("no stored data for %s, keep defaults", key);
		return ODR_OK;
	}
	LOG_DBG("restored OD entry subIndex=%u", entry->subIndexOD);
	return ODR_OK;
}

/* 启动时把持久化的 OD 条目加载回内存 (在 CO_storage_init 之后调用) */
int canopen_storage_load(void)
{
	return settings_load_subtree(STORAGE_KEY_PREFIX);
}

/*
 * 初始化 CANopen storage: 挂 0x1010/0x1011 到 OD, 提供 store/restore 后端.
 *
 * @param storage    CO_storage_t 对象 (应用持有)
 * @param CANmodule  CAN module 对象
 * @param OD         OD_t 对象
 * @param entries    CO_storage_entry_t 数组 (指向 OD 变量 + 元数据)
 * @param entriesCount 数组长度
 * @param storageInitError 输出: 初始化出错时给出错 entry 下标
 * @return 0=成功, 负 errno
 */
int canopen_storage_init(CO_storage_t *storage, CO_CANmodule_t *CANmodule,
			 OD_t *OD, CO_storage_entry_t *entries,
			 uint8_t entriesCount, uint32_t *storageInitError)
{
	CO_ReturnError_t err;
	OD_entry_t *od_1010, *od_1011;
	int ret;

	if (storage == NULL || OD == NULL || entries == NULL ||
	    entriesCount == 0 || storageInitError == NULL) {
		return -EINVAL;
	}

	/* 从 OD 找 0x1010 / 0x1011 条目 (v4: OD_find 返回 OD_entry_t* 或 NULL) */
	od_1010 = OD_find(OD, 0x1010);
	if (od_1010 == NULL) {
		LOG_WRN("OD 0x1010 not found");
	}
	od_1011 = OD_find(OD, 0x1011);
	if (od_1011 == NULL) {
		LOG_WRN("OD 0x1011 not found");
	}

	/* v4 CO_storage_init: 注册 store/restore 回调 + 挂 OD 0x1010/0x1011 */
	err = CO_storage_init(storage, CANmodule, od_1010, od_1011,
			      canopen_storage_store, canopen_storage_restore,
			      entries, entriesCount);
	if (err != CO_ERROR_NO) {
		LOG_ERR("CO_storage_init failed: %d", err);
		return -EIO;
	}

	/* v4 的 CO_storage_t.enabled 默认 false (0x1010/0x1011 写一律
	 * READONLY); example/main_blank.c 由应用手动置位, 本封装代为开启 */
	storage->enabled = true;

	/* 启动时加载持久化数据覆盖 OD 默认值 */
	ret = canopen_storage_load();
	if (ret != 0 && ret != -ENOENT) {
		LOG_WRN("settings_load failed: %d", ret);
	}

	LOG_INF("CANopen storage initialized (%u entries)", entriesCount);
	return 0;
}

#endif /* CONFIG_CANOPENNODE_STORAGE */
