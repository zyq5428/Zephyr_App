#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h> // 建议添加日志头文件，尽管这里只用 printk
#include "led_thread.h"

// 线程栈和定义
#define LED_STACK_SIZE 512
K_THREAD_STACK_DEFINE(led_stack_area, LED_STACK_SIZE);
struct k_thread led_thread_data;

/* 1000 msec = 1 sec */
#define SLEEP_TIME_MS   1000

/* The devicetree node identifier for the "led0" alias. */
#define LED1_NODE DT_ALIAS(led1)

/*
 * A build error on this line means your board is unsupported.
 * See the sample documentation for information on how to fix this.
 */
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED1_NODE, gpios);

// **<<<<<<<< 修改点 1：将 int 改为 void >>>>>>>>**
void led_thread_entry(void *p1, void *p2, void *p3)
{
    // ... 在这里初始化您的 LED 设备 ...
    printk("LED Thread started\n");

    int ret;
	bool led_state = true;

	if (!gpio_is_ready_dt(&led)) {
        // **<<<<<<<< 修改点 2a：处理错误并避免返回 >>>>>>>>**
		printk("Error: LED device not ready\n");
        while(1) { k_msleep(SLEEP_TIME_MS); } // 避免返回，进入无限休眠
	}

	ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
	if (ret < 0) {
        // **<<<<<<<< 修改点 2b：处理错误并避免返回 >>>>>>>>**
		printk("Error: Failed to configure LED pin (ret=%d)\n", ret);
        while(1) { k_msleep(SLEEP_TIME_MS); } // 避免返回，进入无限休眠
	}

    while (1) {
		ret = gpio_pin_toggle_dt(&led);
		if (ret < 0) {
            // **<<<<<<<< 修改点 2c：处理错误并避免返回 >>>>>>>>**
            printk("Error: Failed to toggle LED pin (ret=%d)\n", ret);
            while(1) { k_msleep(SLEEP_TIME_MS); } // 避免返回，进入无限休眠
		}

		led_state = !led_state;
		printk("LED state: %s\n", led_state ? "ON" : "OFF");
		k_msleep(SLEEP_TIME_MS);
    }
}

void start_led_thread(void)
{
    // 创建并启动线程
    k_thread_create(&led_thread_data, 
                    led_stack_area,
                    LED_STACK_SIZE,
                    led_thread_entry,
                    NULL, NULL, NULL,
                    5, K_USER | K_FP_REGS, K_FOREVER);

    k_thread_name_set(&led_thread_data, "LED_Control");
    k_thread_start(&led_thread_data);
}