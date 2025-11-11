#include <zephyr/kernel.h>
#include "led_thread.h"

// 线程栈和定义
#define LED_STACK_SIZE 512
K_THREAD_STACK_DEFINE(led_stack_area, LED_STACK_SIZE);
struct k_thread led_thread_data;

void led_thread_entry(void *p1, void *p2, void *p3)
{
    // ... 在这里初始化您的 LED 设备 ...
    printk("LED Thread started\n");

    while (1) {
        // 实际的 LED 闪烁逻辑
        k_msleep(1000);
    }
}

void start_led_thread(void)
{
    // 创建并启动线程
    k_thread_create(&led_thread_data, led_stack_area,
                    LED_STACK_SIZE,
                    led_thread_entry,
                    NULL, NULL, NULL,
                    5, K_USER | K_FP_REGS, K_FOREVER);

    k_thread_name_set(&led_thread_data, "LED_Control");
    k_thread_start(&led_thread_data);
}