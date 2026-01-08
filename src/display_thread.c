#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <lvgl.h>
#include <stdio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(Display_TASK, LOG_LEVEL_INF);

/* --- UI 句柄 --- */
static lv_obj_t * main_arc;
static lv_obj_t * val_label;
static lv_obj_t * main_chart;
static lv_chart_series_t * main_ser;

/**
 * @brief 定时器回调：模拟数据平滑波动
 */
static void ui_timer_cb(lv_timer_t * t) {
    static int angle = 0;
    angle += 2;
    
    // 产生一个 0-100 往复波动的数值
    int display_val = (angle % 200);
    if(display_val > 100) display_val = 200 - display_val;

    // 更新圆环和文字
    lv_arc_set_value(main_arc, display_val);
    char buf[16];
    snprintf(buf, sizeof(buf), "%d%%", display_val);
    lv_label_set_text(val_label, buf);

    // 将数据推入波形图
    lv_chart_set_next_value(main_chart, main_ser, display_val);
}

void display_thread_entry(void) {
    const struct device *dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
    if (!device_is_ready(dev)) return;

    // 1. 设置你喜欢的蓝黑色背景 (深蓝色 0x001529)
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x001529), 0);

    /* --- 2. 创建圆环进度条 (Arc) --- */
    main_arc = lv_arc_create(lv_scr_act());
    // 尺寸 180x180，留出空间避开 240 宽度的圆角边缘
    lv_obj_set_size(main_arc, 180, 180);
    lv_obj_align(main_arc, LV_ALIGN_CENTER, 0, -35); 
    lv_arc_set_rotation(main_arc, 135);     // 起始角度
    lv_arc_set_bg_angles(main_arc, 0, 270); // 弧度跨度
    lv_arc_set_value(main_arc, 0);
    
    // 样式：青色进度条，加粗显示
    lv_obj_set_style_arc_width(main_arc, 12, LV_PART_MAIN);
    lv_obj_set_style_arc_width(main_arc, 12, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(main_arc, lv_palette_main(LV_PALETTE_CYAN), LV_PART_INDICATOR);
    lv_obj_remove_style(main_arc, NULL, LV_PART_KNOB); // 移除调节手柄

    /* --- 3. 中央大字数值 --- */
    val_label = lv_label_create(lv_scr_act());
    // 请确保 prj.conf 开启了 CONFIG_LV_FONT_MONTSERRAT_48=y
    lv_obj_set_style_text_font(val_label, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(val_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(val_label, LV_ALIGN_CENTER, 0, -35);
    lv_label_set_text(val_label, "0%");

    /* --- 4. 底部实时平滑波形 (Chart) --- */
    main_chart = lv_chart_create(lv_scr_act());
    // 宽度 160，高度 60，向上偏移避开底部圆角
    lv_obj_set_size(main_chart, 160, 65);
    lv_obj_align(main_chart, LV_ALIGN_BOTTOM_MID, 0, -45); 
    lv_chart_set_type(main_chart, LV_CHART_TYPE_LINE);
    
    // 添加波形线 (亮红色)
    main_ser = lv_chart_add_series(main_chart, lv_palette_main(LV_PALETTE_RED), LV_CHART_AXIS_PRIMARY_Y);
    
    // 设置透明样式，融入蓝黑色背景
    lv_obj_set_style_bg_opa(main_chart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(main_chart, 0, 0);
    lv_obj_set_style_line_width(main_chart, 3, LV_PART_ITEMS);

    /* --- 5. 底部装饰文字 --- */
    lv_obj_t * info = lv_label_create(lv_scr_act());
    lv_label_set_text(info, "ST7789V3 | LVGL v9");
    lv_obj_set_style_text_color(info, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_align(info, LV_ALIGN_BOTTOM_MID, 0, -15);

    // 启动 50ms 刷新定时器
    lv_timer_create(ui_timer_cb, 50, NULL);

    while (1) {
        lv_timer_handler();
        k_msleep(10);
    }
}

/* --- 线程定义 --- */
#define DISPLAY_STACK_SIZE 8192
#define DISPLAY_PRIORITY 5
K_THREAD_DEFINE(display_thread_tid, DISPLAY_STACK_SIZE, 
                display_thread_entry, NULL, NULL, NULL,
                DISPLAY_PRIORITY, 0, 0);