/*
 * 文件名: display_thread.c
 * 描述: LVGL 组件切换测试 + 实时内存监控（适配 128x64 OLED）
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <lvgl.h>
#include <stdio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(Display_TASK, LOG_LEVEL_INF);

/* --- 全局变量与定义 --- */
static lv_obj_t * monitor_label;    // 用于显示内存信息的标签
static lv_obj_t * test_area;        // 用于放置测试组件的容器
static uint32_t step = 0;

/**
 * 手动更新内存数据，不使用内置宏
 */
void mem_monitor_task(lv_timer_t * timer) {
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon); // 获取内存数据

    char buf[32];
    // 128x64 屏幕窄，我们显示核心数据即可
    // used_pct: 百分比
    snprintf(buf, sizeof(buf), "Used:%d%%", mon.used_pct);

    if (monitor_label) {
        lv_label_set_text(monitor_label, buf);
    }
}

/* --- 组件切换逻辑 --- */
void switch_test_widget(void) {
    // 每次切换前清空测试区域，但不删除顶部的内存标签
    lv_obj_clean(test_area);
    
    switch (step % 4) {
        case 0: { // 测试按钮
            lv_obj_t * btn = lv_btn_create(test_area);
            lv_obj_set_size(btn, 70, 30);
            lv_obj_align(btn, LV_ALIGN_CENTER, 0, 0);
            lv_obj_t * l = lv_label_create(btn);
            lv_label_set_text(l, "Button");
            lv_obj_center(l);
            break;
        }
        case 1: { // 测试滑块
            lv_obj_t * slider = lv_slider_create(test_area);
            lv_obj_set_size(slider, 100, 10);
            lv_obj_align(slider, LV_ALIGN_CENTER, 0, 0);
            lv_slider_set_value(slider, 50, LV_ANIM_OFF);
            break;
        }
        case 2: { // 测试复选框
            lv_obj_t * cb = lv_checkbox_create(test_area);
            lv_checkbox_set_text(cb, "Monitor ON");
            lv_obj_add_state(cb, LV_STATE_CHECKED);
            lv_obj_align(cb, LV_ALIGN_CENTER, 0, 0);
            break;
        }
        case 3: { // 测试圆弧 (Arc)
            lv_obj_t * arc = lv_arc_create(test_area);
            lv_obj_set_size(arc, 40, 40);
            lv_obj_align(arc, LV_ALIGN_CENTER, 0, 0);
            lv_arc_set_value(arc, 60); // 设置一个初始进度值
            break;
        }
    }
    step++;
}

/* --- 线程入口 --- */
void display_thread_entry(void) 
{
    LOG_INF("Display Thread started");

    const struct device *display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
    if (!device_is_ready(display_dev)) {
        LOG_ERR("Display not ready");
        return;
    }

    // 1. 初始化基础布局
    lv_obj_clean(lv_scr_act()); // 清空屏幕

    // 创建顶部的内存监控标签
    monitor_label = lv_label_create(lv_scr_act());
    lv_obj_align(monitor_label, LV_ALIGN_TOP_RIGHT, -2, 0);

    // 创建一个 LVGL 定时器，每秒更新一次
    // 这样比在 while(1) 里更新更安全，不会抢占渲染资源
    lv_timer_create(mem_monitor_task, 1000, NULL);
    
    // 创建下方的测试组件区域
    test_area = lv_obj_create(lv_scr_act());
    lv_obj_set_size(test_area, 128, 45);
    lv_obj_align(test_area, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_border_width(test_area, 1, 0); // 画个边框区分
    lv_obj_set_style_pad_all(test_area, 2, 0);

    uint32_t last_switch = 0;

    while (1) {
        // 每 2 秒切换一次组件
        if (k_uptime_get_32() - last_switch > 2000) {
            switch_test_widget();
            last_switch = k_uptime_get_32();
        }

        // 运行 LVGL 任务处理器
        lv_timer_handler();
        k_msleep(50);
    }
}

/* --- 线程定义和启动 --- */

// 定义栈空间、优先级和线程入口函数
#define DISPLAY_STACK_SIZE 8192
#define DISPLAY_PRIORITY 5

K_THREAD_DEFINE(display_thread_tid, DISPLAY_STACK_SIZE, 
                display_thread_entry, NULL, NULL, NULL,
                DISPLAY_PRIORITY, 0, 0);
