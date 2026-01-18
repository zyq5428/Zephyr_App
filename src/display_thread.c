#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <lvgl.h>
#include <stdio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(Display_TASK, LOG_LEVEL_INF);

/* 获取显示设备句柄 */
static const struct device *dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

/* --- UI 句柄 --- */
static lv_obj_t * main_arc;
static lv_obj_t * val_label;
static lv_obj_t * main_chart;
static lv_chart_series_t * main_ser;

/**
 * @brief 定时器回调：模拟数据平滑波动 (范围 0-255)
 */
static void ui_timer_cb(lv_timer_t * t) {
    // static 变量在函数调用结束后不会被销毁，会保留上次的值
    static int angle = 0; 
    
    // 每次回调增加步进值，增加越快，数值波动越快
    angle += 2; 
    
    /* * 目标：产生一个 0 到 255 再回到 0 的往复数值
     * 1. 我们取模 510 (即 255 * 2)，这样 display_val 的范围会在 0 到 509 之间循环
     */
    int display_val = (angle % 510);

    /* * 2. 折返逻辑处理：
     * 如果数值在 0-255 之间，直接使用；
     * 如果数值在 256-509 之间，我们需要让它“往回走”。
     * 例如：当 display_val 是 256 时，我们希望得到 254 (即 510 - 256)
     */
    if(display_val > 255) {
        display_val = 510 - display_val;
    }

    // --- 更新 UI 控件 ---

    // 更新圆环的值 (确保你的 main_arc 范围也设置成了 0-255)
    lv_arc_set_value(main_arc, display_val);

    // 更新文本标签显示百分比或数值
    char buf[16];
    // 这里如果依然想显示百分比，需要计算：(val / 255.0) * 100
    // 如果直接显示原始值，使用下面的代码：
    snprintf(buf, sizeof(buf), "%d", display_val);
    lv_label_set_text(val_label, buf);

    // 将数据推入波形图 (Chart 控件会自动根据设置的 Range 绘制)
    lv_chart_set_next_value(main_chart, main_ser, display_val);

    // 动态调节硬件亮度 (通常 PWM 亮度范围就是 0-255)
    display_set_brightness(dev, display_val); 
}

void display_thread_entry(void) 
{
    LOG_INF("Display Thread started");
    
    if (!device_is_ready(dev)) {
        LOG_ERR("Display device not ready");
        return;
    }

    // 1. 设置你喜欢的蓝黑色背景 (深蓝色 0x001529)
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x001529), 0);

    /* --- 2. 创建圆环进度条 (Arc) --- */
    main_arc = lv_arc_create(lv_scr_act());
    // 尺寸 180x180，留出空间避开 240 宽度的圆角边缘
    lv_obj_set_size(main_arc, 180, 180);
    lv_obj_align(main_arc, LV_ALIGN_CENTER, 0, -35); 
    /* --- 设置圆环开口方向 --- */
    // 135度(左下) 到 45度(右下)，顺时针经过顶部，底部留出 90 度缺口
    lv_arc_set_bg_angles(main_arc, 135, 45);
    /* --- 设置数据范围 --- */
    // 必须设置这个，否则 101-255 的数值会导致圆环卡死在满格
    lv_arc_set_range(main_arc, 0, 255); 

    /* --- 初始化值 --- */
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
    // 设置图表纵轴的取值范围为 0 到 255, LVGL v8/v9 的语法：设置主 Y 轴范围
    lv_chart_set_range(main_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 255);
    
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