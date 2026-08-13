/*
 * Copyright (c) 2026 Kabirz.
 * SPDX-License-Identifier: Apache-2.0
 *
 * CANopen LED 指示 (CiA 303-3) - Zephyr GPIO 实现 (v4).
 *
 * v4 CO_LEDs API:
 *   - CO_LEDs_init()     初始化 LED 对象
 *   - CO_LEDs_process()  周期更新 LEDred/LEDgreen 位域 (CiA 303-3 状态机)
 *   - CO_LED_RED/GREEN(LEDs, CO_LED_CANopen) 宏读出 ON/OFF, 应用驱动 GPIO
 *
 * 本模块: 提供 GPIO 封装 + canopen_leds_process() 便利函数.
 * 用 devicetree 的 green_led / red_led 别名 (可选, 未定义则静默跳过).
 */

#if defined(CONFIG_CANOPENNODE_LEDS)

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include "303/CO_LEDs.h"
#include "301/CO_driver.h"

LOG_MODULE_REGISTER(canopennode_leds, CONFIG_CANOPENNODE_LOG_LEVEL);

/* GPIO 规格 (可选的 aliases: green_led / red_led) */
static struct gpio_dt_spec led_green_gpio = GPIO_DT_SPEC_GET_OR(
	DT_ALIAS(green_led), gpios, {0});
static struct gpio_dt_spec led_red_gpio = GPIO_DT_SPEC_GET_OR(
	DT_ALIAS(red_led), gpios, {0});

/*
 * 初始化 LED GPIO + CO_LEDs 对象.
 *
 * @param LEDs CO_LEDs_t 对象 (应用持有, 如 sample main 的局部变量)
 * @return 0=成功, 负 errno
 */
int canopen_leds_init(CO_LEDs_t *LEDs)
{
	if (LEDs == NULL) {
		return -EINVAL;
	}

	/* 配置 GPIO 输出 (未定义别名时跳过) */
	if (led_green_gpio.port != NULL &&
	    gpio_is_ready_dt(&led_green_gpio)) {
		if (gpio_pin_configure_dt(&led_green_gpio,
					  GPIO_OUTPUT_INACTIVE) != 0) {
			LOG_WRN("green_led GPIO configure failed");
		}
	}
	if (led_red_gpio.port != NULL && gpio_is_ready_dt(&led_red_gpio)) {
		if (gpio_pin_configure_dt(&led_red_gpio,
					  GPIO_OUTPUT_INACTIVE) != 0) {
			LOG_WRN("red_led GPIO configure failed");
		}
	}

	CO_LEDs_init(LEDs);

	LOG_INF("CANopen LEDs initialized");
	return 0;
}

/*
 * 周期处理 LED (应用 main 循环调用, 与 CO_process 同周期).
 * 内部调 CO_LEDs_process 更新状态机, 然后按 CO_LED_CANopen 驱动 GPIO.
 *
 * @param LEDs   CO_LEDs_t 对象
 * @param time_difference_us 距上次调用微秒
 * @param NMTstate  NMT 状态 (从 CO->NMT 取)
 * @param em        错误对象 (从 CO->em 取)
 */
void canopen_leds_process(CO_LEDs_t *LEDs, uint32_t time_difference_us,
			  CO_NMT_internalState_t NMTstate, CO_EM_t *em)
{
	uint32_t timer_next_us = 0;
	bool err_off, err_warn, err_rpdo, err_sync, err_hbcons, err_other;

	if (LEDs == NULL) {
		return;
	}

	/* 从 EM 错误寄存器取各种错误标志 (v4 错误码名) */
	err_off = CO_isError(em, CO_EM_CAN_TX_BUS_OFF);
	err_warn = CO_isError(em, CO_EM_CAN_BUS_WARNING);
	err_rpdo = false;   /* RPDO 事件超时 - 默认 OD 无 RPDO timeout 配置, 置 false */
	err_sync = CO_isError(em, CO_EM_SYNC_TIME_OUT);
	err_hbcons = CO_isError(em, CO_EM_HB_CONSUMER_REMOTE_RESET);
	err_other = CO_isError(em, CO_EM_GENERIC_ERROR);

	CO_LEDs_process(LEDs, time_difference_us, NMTstate, false,
			err_off, err_warn, err_rpdo, err_sync, err_hbcons,
			err_other, false, &timer_next_us);

	/* 驱动 GPIO (CiA 303-3: 绿色 = CANopen 状态, 红色 = 错误) */
	if (led_green_gpio.port != NULL) {
		gpio_pin_set_dt(&led_green_gpio,
				CO_LED_GREEN(LEDs, CO_LED_CANopen));
	}
	if (led_red_gpio.port != NULL) {
		gpio_pin_set_dt(&led_red_gpio,
				CO_LED_RED(LEDs, CO_LED_CANopen));
	}
}

#endif /* CONFIG_CANOPENNODE_LEDS */
