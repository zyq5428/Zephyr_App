#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include "led_thread.h"

// 启用日志记录
LOG_MODULE_REGISTER(LED_TASK, LOG_LEVEL_INF);

/* 1000 msec = 1 sec */
#define SLEEP_TIME_MS   1000

/* The devicetree node identifier for the "led0" alias. */
#define LED1_NODE DT_ALIAS(led1)

/* GPIO_DT_SPEC_GET(node_id, property)： 这是Zephyr提供的另一个宏，
* 用于从指定的设备节点ID (LED1_NODE) 中提取名为 property 的属性值，
* 并将其封装成 struct gpio_dt_spec。
* 这里的 gpios 是一个标准的设备树属性名。
*/
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED1_NODE, gpios);

void led_thread_entry(void *p1, void *p2, void *p3)
{
    // ... 在这里初始化您的 LED 设备 ...
    LOG_INF("LED Thread started");

    int ret;
	bool led_state = true;

	if (!gpio_is_ready_dt(&led)) {
		LOG_ERR("Error: LED device not ready");
        while(1) { k_msleep(SLEEP_TIME_MS); } // 避免返回，进入无限休眠
	}

	ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
	if (ret < 0) {
		LOG_ERR("Error: Failed to configure LED pin (ret=%d)", ret);
        while(1) { k_msleep(SLEEP_TIME_MS); } // 避免返回，进入无限休眠
	}

    while (1) {
		ret = gpio_pin_toggle_dt(&led);
		if (ret < 0) {
            LOG_ERR("Error: Failed to toggle LED pin (ret=%d)", ret);
            while(1) { k_msleep(SLEEP_TIME_MS); } // 避免返回，进入无限休眠
		}

		led_state = !led_state;
		LOG_DBG("LED state: %s", led_state ? "ON" : "OFF");
		k_msleep(SLEEP_TIME_MS);
    }
}

// 线程栈和定义
#define LED_STACK_SIZE 512
#define LED_PRIORITY 10

K_THREAD_DEFINE(led_tid, LED_STACK_SIZE, 
                led_thread_entry, NULL, NULL, NULL,
                LED_PRIORITY, 0, 0);

void start_led_thread(void)
{
    // 线程已在 K_THREAD_DEFINE 中自动启动，此函数仅用于提供统一的启动接口
    LOG_DBG("LED thread defined and started.");
}