/*
 * 文件名: display_thread.c
 * 描述: LVGL 显示测试线程，用于验证 SH1106 OLED 屏幕是否正常工作。
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/gpio.h>
#include <lvgl.h>  // 包含 LVGL 核心头文件
#include <stdio.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(Display_TASK, LOG_LEVEL_INF);


/* 显示测试主逻辑 */
void display_thread_entry(void)
{
    const struct device *display_dev;
    uint32_t count = 0;
    char count_str[20];

    LOG_INF("Display Thread started");

    // 确认显示设备是否就绪
    // 这里使用了 DT_CHOSEN(zephyr_display)，它会自动指向你在 overlay 里设置的 &sh1106_oled
    display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
    if (!device_is_ready(display_dev)) {
        LOG_ERR("显示设备未就绪！");
        return;
    }

    // 创建 LVGL 界面元素
    // 在 LVGL 中，lv_scr_act() 获取当前活跃屏幕
    lv_obj_t *hello_label = lv_label_create(lv_scr_act());
    lv_label_set_text(hello_label, "Hello Zephyr!");
    
    // 设置标签居中
    lv_obj_align(hello_label, LV_ALIGN_CENTER, 0, -10);

    // 创建第二个标签显示计数器
    lv_obj_t *count_label = lv_label_create(lv_scr_act());
    if (count_label == NULL) {
        printk("内存池不足，标签创建失败！\n");
    } else {
        lv_label_set_text(count_label, "Hello Zephyr!");
    }
    lv_obj_align(count_label, LV_ALIGN_CENTER, 0, 10);

    LOG_INF("开始显示循环...");

    while (1) {
        // 更新计数器内容
        snprintf(count_str, sizeof(count_str), "Count: %d", count++);
        lv_label_set_text(count_label, count_str);

        // 关键：LVGL 任务处理器
        // LVGL 需要定期调用这个函数来刷新屏幕、处理定时器等。
        // 在 Zephyr 中，lvgl_timer_handler 会自动处理底层驱动的 write 调用。
        lv_timer_handler();

        // g. 延时，释放 CPU 给其他线程
        // 建议 10ms - 50ms 左右，保证动画流畅且不占满 CPU
        k_sleep(K_MSEC(50));
    }
}

/* --- 线程定义和启动 --- */

// 定义栈空间、优先级和线程入口函数
#define DISPLAY_STACK_SIZE 8192
#define DISPLAY_PRIORITY 5

K_THREAD_DEFINE(display_thread_tid, DISPLAY_STACK_SIZE, 
                display_thread_entry, NULL, NULL, NULL,
                DISPLAY_PRIORITY, 0, 0);

void start_display_thread(void)
{
    // 线程已在 K_THREAD_DEFINE 中自动启动，此函数仅用于提供统一的启动接口
    LOG_DBG("Display thread defined and started.");
}