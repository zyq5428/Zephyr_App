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

// --- 引用外部全局变量 ---
// extern volatile uint16_t g_als_raw_value;
// --- 引用外部定义的消息队列 ---
extern struct k_msgq als_msgq;

/* --- 全局变量与定义 --- */
static lv_obj_t * monitor_label;    // 用于显示内存信息的标签
static lv_obj_t * als_label;        // 左上角光感值标签
static lv_obj_t * als_slider;       // --- 新增：光感进度条 ---
static lv_obj_t * test_area;        // 用于放置测试组件的容器
static uint32_t step = 0;

/**
 * 此时 UI 更新函数只负责显示“最新”的数据
 * 我们用一个静态变量保存从队列拿到的值
 */
static uint16_t last_received_als = 0;

void ui_update_task(lv_timer_t * timer) {
    // 更新内存显示 (原有逻辑)
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    char mem_buf[16];
    snprintf(mem_buf, sizeof(mem_buf), "M:%d%%", mon.used_pct);
    lv_label_set_text(monitor_label, mem_buf);

    // 尝试从队列中读取数据
    // K_NO_WAIT 表示如果没有新消息，立刻返回，不阻塞 LVGL 渲染
    uint16_t temp_val;
    if (k_msgq_get(&als_msgq, &temp_val, K_NO_WAIT) == 0) {
        last_received_als = temp_val; // 只有拿到新消息才更新
    }

    // 更新标签和滑块
    char buf[20];
    snprintf(buf, sizeof(buf), "ALS:%u", last_received_als);
    lv_label_set_text(als_label, buf);

    // 更新进度条数值 ---
    // AP3216C 在当前配置下的最大值约为 20661
    // 我们将滑块范围设为 0-1000，方便观察细微变化
    // 使用 lv_slider_set_value(对象, 数值, 是否使用动画)
    lv_slider_set_value(als_slider, last_received_als, LV_ANIM_ON);
}

/* --- 组件切换逻辑 --- */
void switch_test_widget(void) {
    // 每次切换前清空测试区域，但不删除顶部的内存标签
    lv_obj_clean(test_area);
    
    switch (step % 4) {
        case 0: { 
            lv_obj_t * btn = lv_btn_create(test_area);
            lv_obj_set_size(btn, 60, 20);
            lv_obj_align(btn, LV_ALIGN_CENTER, 0, 0);
            lv_obj_t * l = lv_label_create(btn);
            lv_label_set_text(l, "OK");
            lv_obj_center(l);
            break;
        }
        case 1: { 
            lv_obj_t * cb = lv_checkbox_create(test_area);
            lv_checkbox_set_text(cb, "Auto");
            lv_obj_align(cb, LV_ALIGN_CENTER, 0, 0);
            break;
        }
        case 2: { 
            lv_obj_t * bar = lv_bar_create(test_area);
            lv_obj_set_size(bar, 80, 10);
            lv_obj_align(bar, LV_ALIGN_CENTER, 0, 0);
            lv_bar_set_value(bar, 70, LV_ANIM_OFF);
            break;
        }
        case 3: { 
            lv_obj_t * led = lv_led_create(test_area);
            lv_obj_set_size(led, 15, 15);
            lv_obj_align(led, LV_ALIGN_CENTER, 0, 0);
            lv_led_on(led);
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

    // --- 创建右上角内存标签 ---
    monitor_label = lv_label_create(lv_scr_act());
    lv_obj_align(monitor_label, LV_ALIGN_TOP_RIGHT, -2, 0);

    // --- 新增：创建左上角光感值标签 ---
    als_label = lv_label_create(lv_scr_act());
    lv_obj_align(als_label, LV_ALIGN_TOP_LEFT, 2, 0);

    // 新增：创建中间的进度条 (Slider) ---
    als_slider = lv_slider_create(lv_scr_act());
    lv_obj_set_size(als_slider, 100, 8); // 高度稍微减小到 8 像素，节省空间
    // LV_ALIGN_TOP_MID 表示对齐到顶部中间，y=20 表示距离顶部 20 像素（避开了标签）
    lv_obj_align(als_slider, LV_ALIGN_TOP_MID, 0, 20);
    
    // 设置滑块范围：0 到 20661 (对应传感器的最大量程)
    lv_slider_set_range(als_slider, 0, 20661);
    // 禁用用户手动拖动（因为它是用来展示数据的，不是输入的）
    lv_obj_clear_flag(als_slider, LV_OBJ_FLAG_CLICKABLE);

    // 创建一个 LVGL 定时器，每秒更新一次
    // 这样比在 while(1) 里更新更安全，不会抢占渲染资源
    // 修改定时器：每 500ms 更新一次 UI 数据
    lv_timer_create(ui_update_task, 500, NULL);
    
    // 下方测试区域：强制对齐到底部
    test_area = lv_obj_create(lv_scr_act());
    // 高度缩小为 25 像素，防止它往上生长
    lv_obj_set_size(test_area, 128, 25); 
    // 对齐到底部中心
    lv_obj_align(test_area, LV_ALIGN_BOTTOM_MID, 0, 0);
    // 关键：移除容器的内边距(Padding)和边框，这样里面的内容不会显得太拥挤
    lv_obj_set_style_pad_all(test_area, 0, 0); 
    lv_obj_set_style_border_width(test_area, 1, 0); // 保留 1 像素边框

    uint32_t last_switch = 0;

    while (1) {
        // 每 2 秒切换一次组件
        if (k_uptime_get_32() - last_switch > 3000) {
            switch_test_widget();
            last_switch = k_uptime_get_32();
        }

        // 运行 LVGL 任务处理器
        lv_timer_handler();
        k_msleep(30);
    }
}

/* --- 线程定义和启动 --- */

// 定义栈空间、优先级和线程入口函数
#define DISPLAY_STACK_SIZE 8192
#define DISPLAY_PRIORITY 5

K_THREAD_DEFINE(display_thread_tid, DISPLAY_STACK_SIZE, 
                display_thread_entry, NULL, NULL, NULL,
                DISPLAY_PRIORITY, 0, 0);
