#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <lvgl.h>
#include <stdio.h>

// 定义测试颜色：使用 LVGL 的颜色宏
static lv_color_t test_colors[] = {
    LV_COLOR_MAKE(0xFF, 0x00, 0x00), // 红色 RED
    LV_COLOR_MAKE(0x00, 0xFF, 0x00), // 绿色 GREEN
    LV_COLOR_MAKE(0x00, 0x00, 0xFF)  // 蓝色 BLUE
};

static uint8_t color_index = 0;
static lv_obj_t *full_screen_rect; // 全屏矩形对象

/**
 * 颜色切换定时器回调函数
 * 每隔一段时间被调用一次
 */
static void color_switch_timer_cb(lv_timer_t * timer)
{
    // 1. 切换颜色索引 (0 -> 1 -> 2 -> 0)
    color_index = (color_index + 1) % 3;

    // 2. 设置全屏矩形的背景颜色
    // lv_obj_set_style_bg_color(对象, 颜色, 状态)
    lv_obj_set_style_bg_color(full_screen_rect, test_colors[color_index], LV_PART_MAIN);

    // 打印当前颜色，方便在串口调试终端观察
    if (color_index == 0) printf("Screen set to: RED\n");
    else if (color_index == 1) printf("Screen set to: GREEN\n");
    else printf("Screen set to: BLUE\n");
}

/* --- 线程入口 --- */
void display_thread_entry(void) 
{
    // --- 步骤 1: 创建一个充满全屏的矩形对象 ---
    // lv_scr_act() 获取当前活跃屏幕
    full_screen_rect = lv_obj_create(lv_scr_act());
    
    // 设置大小为 100% (充满屏幕)
    lv_obj_set_size(full_screen_rect, LV_PCT(100), LV_PCT(100));
    
    // 移除默认的边框和圆角，确保颜色铺满全屏
    lv_obj_set_style_radius(full_screen_rect, 0, 0);
    lv_obj_set_style_border_width(full_screen_rect, 0, 0);
    
    // 初始颜色设置为红色
    lv_obj_set_style_bg_color(full_screen_rect, test_colors[0], LV_PART_MAIN);
    lv_obj_set_style_bg_opa(full_screen_rect, LV_OPA_COVER, LV_PART_MAIN);

    // --- 步骤 2: 创建 LVGL 定时器 ---
    // 每 1000 毫秒 (1秒) 执行一次颜色切换
    lv_timer_create(color_switch_timer_cb, 1000, NULL);

    while (1) {
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
