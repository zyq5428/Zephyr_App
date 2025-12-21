/*
 * 文件名: display_thread.c
 * 描述: 针对 128x64 OLED 的全功能组件循环测试框架
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <lvgl.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(Display_TASK, LOG_LEVEL_INF);

/* --- 状态定义 --- */
typedef enum {
    TEST_LABEL,
    TEST_BUTTON,
    TEST_BAR,
    TEST_CHECKBOX,
    TEST_CHART,
    TEST_MAX
} test_state_t;

static test_state_t current_test = TEST_LABEL;

/* --- UI 切换函数 --- */

// 1. 测试文本和中文字体 (对应你开启的 UTF8 和 Font 设置)
void show_test_label(void) {
    lv_obj_clean(lv_scr_act());
    lv_obj_t * label = lv_label_create(lv_scr_act());
    lv_label_set_text(label, "LVGL Test\n128x64 OLED"); // 默认使用 Montserrat 14
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);
}

// 2. 测试按钮和单色主题 (对应你开启的 Monochrome Theme)
void show_test_button(void) {
    lv_obj_clean(lv_scr_act());
    lv_obj_t * btn = lv_btn_create(lv_scr_act());
    lv_obj_set_size(btn, 80, 30);
    lv_obj_center(btn);

    lv_obj_t * label = lv_label_create(btn);
    lv_label_set_text(label, "Button"); // 按钮组件
    lv_obj_center(label);
}

// 3. 测试进度条 (Bar)
void show_test_bar(void) {
    lv_obj_clean(lv_scr_act());
    lv_obj_t * bar = lv_bar_create(lv_scr_act());
    lv_obj_set_size(bar, 100, 15);
    lv_obj_center(bar);
    lv_bar_set_value(bar, 70, LV_ANIM_ON); // 进度条组件
}

// 4. 测试复选框 (Checkbox)
void show_test_checkbox(void) {
    lv_obj_clean(lv_scr_act());
    lv_obj_t * cb = lv_checkbox_create(lv_scr_act());
    lv_checkbox_set_text(cb, "Select Me");
    lv_obj_add_state(cb, LV_STATE_CHECKED); // 初始选中状态
    lv_obj_center(cb);
}

// 5. 测试图表 (Chart)
void show_test_chart(void) {
    lv_obj_clean(lv_scr_act());
    lv_obj_t * chart = lv_chart_create(lv_scr_act());
    lv_obj_set_size(chart, 100, 50);
    lv_obj_center(chart);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE); // 线条图表
    
    lv_chart_series_t * ser = lv_chart_add_series(chart, lv_palette_main(LV_PALETTE_RED), LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_next_value(chart, ser, 10);
    lv_chart_set_next_value(chart, ser, 40);
    lv_chart_set_next_value(chart, ser, 20);
}

/* --- 核心控制逻辑 --- */

void display_thread_entry(void) {
    LOG_INF("OLED Multi-Widget Test Started");

    const struct device *display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
    if (!device_is_ready(display_dev)) {
        LOG_ERR("Display device not ready!");
        return;
    }

    uint32_t last_switch_time = k_uptime_get_32();

    while (1) {
        // 每 3 秒自动切换一个功能模块
        if (k_uptime_get_32() - last_switch_time > 3000) {
            current_test = (current_test + 1) % TEST_MAX;
            last_switch_time = k_uptime_get_32();

            switch (current_test) {
                case TEST_LABEL:    show_test_label(); break;
                case TEST_BUTTON:   show_test_button(); break;
                case TEST_BAR:      show_test_bar(); break;
                case TEST_CHECKBOX: show_test_checkbox(); break;
                case TEST_CHART:    show_test_chart(); break;
                default: break;
            }
            LOG_INF("Switched to test: %d", current_test);
        }

        // 必须持续调用以保持 UI 刷新和动画运行
        lv_timer_handler(); 
        k_msleep(30);
    }
}

/* --- 线程定义和启动 --- */

// 定义栈空间、优先级和线程入口函数
#define DISPLAY_STACK_SIZE 4096
#define DISPLAY_PRIORITY 5

K_THREAD_DEFINE(display_thread_tid, DISPLAY_STACK_SIZE, 
                display_thread_entry, NULL, NULL, NULL,
                DISPLAY_PRIORITY, 0, 0);
