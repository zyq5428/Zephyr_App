/*
 * application/src/display_thread.c
 * 显示与UI交互线程 - Pandora Dashboard (Fixed Layout & Value Update)
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pwm.h>
#include <lvgl.h>
#include <stdio.h>
#include <zephyr/logging/log.h>

/* 引入传感器头文件 */
#include "aht10.h"
#include "ap3216c.h"
#include "icm20608.h"

LOG_MODULE_REGISTER(Display_TASK, LOG_LEVEL_INF);

/* -------------------------------------------------------------------------- */
/* 硬件抽象层 (HAL) - 背光控制                              */
/* -------------------------------------------------------------------------- */

static const struct device *dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

/* --- 按键硬件定义 --- */
static const struct gpio_dt_spec btn_up    = GPIO_DT_SPEC_GET(DT_ALIAS(sw_up), gpios);
static const struct gpio_dt_spec btn_down  = GPIO_DT_SPEC_GET(DT_ALIAS(sw_down), gpios);
static const struct gpio_dt_spec btn_left  = GPIO_DT_SPEC_GET(DT_ALIAS(sw_left), gpios);
static const struct gpio_dt_spec btn_right = GPIO_DT_SPEC_GET(DT_ALIAS(sw_right), gpios);

/* 全局输入组句柄 */
static lv_group_t * input_group;

#define HAS_PWM_BL  DT_NODE_HAS_STATUS(DT_ALIAS(pwm_backlight), okay)
#define HAS_GPIO_BL DT_NODE_HAS_STATUS(DT_ALIAS(gpio_backlight), okay)

#if HAS_PWM_BL
static const struct pwm_dt_spec bl_pwm = PWM_DT_SPEC_GET(DT_ALIAS(pwm_backlight));
#endif

#if HAS_GPIO_BL
static const struct gpio_dt_spec bl_gpio = GPIO_DT_SPEC_GET(DT_ALIAS(gpio_backlight), gpios);
#endif

/* 定义平滑系数 */
#define BACKLIGHT_SMOOTH_ALPHA 0.15f  // 数值越小越平滑，建议范围 0.1 - 0.2
static float current_backlight_f = 100.0f; // 记录当前的浮点亮度值

/* 设置背光亮度 */
static int backlight_set(uint8_t brightness)
{
#if HAS_PWM_BL
    if (pwm_is_ready_dt(&bl_pwm)) {
        uint32_t pulse = (bl_pwm.period * brightness) / 255;
        return pwm_set_dt(&bl_pwm, bl_pwm.period, pulse);
    }
#endif

#if HAS_GPIO_BL
    if (gpio_is_ready_dt(&bl_gpio)) {
        return gpio_pin_set_dt(&bl_gpio, brightness > 0 ? 1 : 0);
    }
#endif
    return -ENOTSUP; 
}

/* 初始化背光 */
int backlight_init(void)
{
    int ret = -ENODEV;
#if HAS_PWM_BL
    if (device_is_ready(bl_pwm.dev)) {
        if (pwm_is_ready_dt(&bl_pwm)) {
            LOG_INF("Backlight: PWM Mode Ready");
            return 0; 
        }
    }
#endif
#if HAS_GPIO_BL
    if (gpio_is_ready_dt(&bl_gpio)) {
        ret = gpio_pin_configure_dt(&bl_gpio, GPIO_OUTPUT_INACTIVE);
        if (ret == 0) return 0;
    }
#endif
    return ret;
}

/* -------------------------------------------------------------------------- */
/* UI 逻辑层                                     */
/* -------------------------------------------------------------------------- */

/* 全局对象句柄 */
static lv_obj_t * meter_temp;    // 温度表圆环
static lv_obj_t * meter_humi;    // 湿度表圆环
static lv_obj_t * label_temp_val; // 【新增】温度数值文字句柄
static lv_obj_t * label_humi_val; // 【新增】湿度数值文字句柄

static lv_obj_t * label_accel;   // IMU文字
static lv_obj_t * label_lux;     // 光照文字
static lv_obj_t * chart_light;   // 光照图表 (注意这里是 lv_obj_t*)
static lv_chart_series_t * ser_lux; 

// 缓存数据
static uint16_t cached_lux = 0;
static float cached_temp = 0.0f;
static float cached_humi = 0.0f;

/**
 * @brief 创建仪表盘辅助函数
 * @param label_store: 一个指向 lv_obj_t* 的指针，用来把内部创建的 label 句柄传出去
 */
static lv_obj_t* create_sensor_meter(lv_obj_t* parent, const char* title, const char* unit, 
                                     int x, int y, lv_color_t color, lv_obj_t ** label_store) {
    // 1. 创建圆环
    lv_obj_t* arc = lv_arc_create(parent);
    lv_obj_set_size(arc, 70, 70); 
    lv_obj_align(arc, LV_ALIGN_TOP_LEFT, x, y);
    lv_arc_set_rotation(arc, 135);
    lv_arc_set_bg_angles(arc, 0, 270);
    lv_arc_set_value(arc, 0);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB); // 移除旋钮
    lv_obj_set_style_arc_color(arc, color, LV_PART_INDICATOR);
    
    // 2. 创建中间的数值 Label
    lv_obj_t* val_label = lv_label_create(arc);
    lv_obj_center(val_label);
    lv_label_set_text(val_label, "0"); // 初始值
    lv_obj_set_style_text_color(val_label, lv_color_white(), 0);
    
    // 【关键】将这个 label 的句柄赋值给传入的指针，这样外部就能控制它了
    if (label_store != NULL) {
        *label_store = val_label;
    }

    // 3. 创建底部的标题 Label
    lv_obj_t* title_label = lv_label_create(parent);
    lv_label_set_text_fmt(title_label, "%s %s", title, unit);
    lv_obj_align_to(title_label, arc, LV_ALIGN_OUT_BOTTOM_MID, 0, 5);
    lv_obj_set_style_text_color(title_label, lv_palette_main(LV_PALETTE_GREY), 0);
    
    return arc;
}

/**
 * @brief 传感器圆环的事件处理函数
 * @param e 事件指针，包含了触发事件的对象、类型等信息
 */
static void sensor_arc_event_handler(lv_event_t * e)
{
    /* 从事件中提取出主角：那个被点击的对象（圆环） */
    lv_obj_t * arc = lv_event_get_target(e);

    /* 获取当前的事件代码 */
    lv_event_code_t code = lv_event_get_code(e);

    /* 如果是点击事件（对应你映射的确认键） */
    if (code == LV_EVENT_KEY) {
        /* 将圆环颜色改为绿色 */
        uint32_t key = lv_indev_get_key(lv_indev_get_act());

        if(key == LV_KEY_ENTER) {
            /* 下键确认：变绿 */
            lv_obj_set_style_arc_color(arc, lv_palette_main(LV_PALETTE_GREEN), LV_PART_INDICATOR);
        } 
        else if(key == LV_KEY_ESC) {
            /* 上键返回：变回橙色 */
            lv_obj_set_style_arc_color(arc, lv_palette_main(LV_PALETTE_ORANGE), LV_PART_INDICATOR);
        }
    }
}

/**
 * @brief 初始化 UI 布局
 */
void setup_pandora_dashboard(void) {
    // 背景
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x001529), 0);

    // --- 左侧：温度表 ---
    // 传入 &label_temp_val，让我们可以控制中间的数字
    meter_temp = create_sensor_meter(lv_scr_act(), "Temp", "C", 15, 25, 
                                     lv_palette_main(LV_PALETTE_ORANGE), &label_temp_val);
    lv_arc_set_range(meter_temp, -10, 50);

    // --- 左侧：湿度表 ---
    // 传入 &label_humi_val
    meter_humi = create_sensor_meter(lv_scr_act(), "Humi", "%", 15, 125, 
                                     lv_palette_main(LV_PALETTE_CYAN), &label_humi_val);
    lv_arc_set_range(meter_humi, 0, 100);

    // --- 右上角：IMU 数据 ---
    lv_obj_t* imu_cont = lv_obj_create(lv_scr_act());
    lv_obj_set_size(imu_cont, 120, 100);
    lv_obj_align(imu_cont, LV_ALIGN_TOP_RIGHT, -10, 25);
    lv_obj_set_style_bg_opa(imu_cont, LV_OPA_20, 0); 
    lv_obj_set_style_border_color(imu_cont, lv_color_hex(0x00AEEF), 0);
    lv_obj_set_style_radius(imu_cont, 8, 0);

    label_accel = lv_label_create(imu_cont);
    lv_label_set_text(label_accel, "IMU Data:\nWaiting...");
    lv_obj_set_style_text_color(label_accel, lv_color_white(), 0);
#ifdef CONFIG_LV_FONT_MONTSERRAT_12
    lv_obj_set_style_text_font(label_accel, &lv_font_montserrat_12, 0);
#endif

    // --- 右下角：光照图表 (布局修复核心) ---
    chart_light = lv_chart_create(lv_scr_act());
    // 1. 宽度改小：从 210 改为 110，留出左边给湿度表
    // 2. 高度微调：设为 60
    lv_obj_set_size(chart_light, 110, 60);
    // 3. 对齐方式：改为右下角 (BOTTOM_RIGHT)
    lv_obj_align(chart_light, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
    
    lv_chart_set_type(chart_light, LV_CHART_TYPE_LINE);
    lv_obj_set_style_bg_opa(chart_light, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(chart_light, 0, 0);
    
    // 设置Y轴范围 (0-500 Lux)
    lv_chart_set_range(chart_light, LV_CHART_AXIS_PRIMARY_Y, 0, 500);
    // 设置线条颜色
    ser_lux = lv_chart_add_series(chart_light, lv_color_hex(0xFFFF00), LV_CHART_AXIS_PRIMARY_Y);

    // --- 顶部：光照数值 ---
    label_lux = lv_label_create(lv_scr_act());
    lv_obj_align(label_lux, LV_ALIGN_TOP_MID, 0, 5);
    lv_obj_set_style_text_color(label_lux, lv_color_hex(0xFFFF00), 0);
    lv_label_set_text(label_lux, "Lux: 0");

    // --- 实验：让仪表可以被按键控制 ---
    /* 1. 允许对象被聚焦（这样按键才能选中它） */
    lv_obj_add_flag(meter_temp, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(meter_humi, LV_OBJ_FLAG_CLICKABLE);
    
    /* 2. 手动加入组（虽然设置了默认组，但某些特殊控件需要手动确认） */
    lv_group_add_obj(input_group, meter_temp);
    lv_group_add_obj(input_group, meter_humi);

    /* 3. 添加事件：按下“确认键”（你的下键）时，改变圆环颜色 */
    lv_obj_add_event_cb(meter_temp, sensor_arc_event_handler, LV_EVENT_KEY, NULL);
    lv_obj_add_event_cb(meter_humi, sensor_arc_event_handler, LV_EVENT_KEY, NULL);
}

/**
 * @brief LVGL 键盘读取回调函数
 */
static void keypad_read_cb(lv_indev_t * indev, lv_indev_data_t * data)
{
    data->state = LV_INDEV_STATE_REL; 

    /* 下=确认, 上=返回, 左=左, 右=右 */
    if (gpio_pin_get_dt(&btn_down) > 0) {
        data->state = LV_INDEV_STATE_PR;
        data->key = LV_KEY_ENTER;
        LOG_INF("Down Key Pressed!");
    } else if (gpio_pin_get_dt(&btn_up) > 0) {
        data->state = LV_INDEV_STATE_PR;
        data->key = LV_KEY_ESC;
        LOG_INF("Up Key Pressed!");
    } else if (gpio_pin_get_dt(&btn_left) > 0) {
        data->state = LV_INDEV_STATE_PR;
        data->key = LV_KEY_LEFT;
        LOG_INF("Left Key Pressed!");
    } else if (gpio_pin_get_dt(&btn_right) > 0) {
        data->state = LV_INDEV_STATE_PR;
        data->key = LV_KEY_RIGHT;
        LOG_INF("Right Key Pressed!");
    }
}

/**
 * @brief 定时器回调：刷新数据
 */
static void ui_timer_cb(lv_timer_t * t) {
    
    /* --- 1. 光照数据处理 --- */
    uint16_t als_val;
    if (k_msgq_get(&als_msgq, &als_val, K_NO_WAIT) == 0) {
        cached_lux = als_val; 
        
        lv_chart_set_next_value(chart_light, ser_lux, cached_lux);
        lv_label_set_text_fmt(label_lux, "Lux: %d", cached_lux);

        // 背光控制逻辑
        // A. 确定目标亮度（限定在 20-255 之间）
        float target_bl = (float)cached_lux;
        if (target_bl > 255.0f) target_bl = 255.0f;
        if (target_bl < 20.0f) target_bl = 20.0f;

        // B. 一阶低通滤波计算
        // 核心逻辑：当前亮度 = 旧亮度 * 0.85 + 目标亮度 * 0.15
        current_backlight_f = (current_backlight_f * (1.0f - BACKLIGHT_SMOOTH_ALPHA)) + 
                              (target_bl * BACKLIGHT_SMOOTH_ALPHA);

        // C. 应用平滑后的亮度
        backlight_set((uint8_t)current_backlight_f);
    }

    /* --- 2. 温湿度数据处理 (数值显示修复核心) --- */
    aht10_data_t sensor_data;
    if (k_msgq_get(&aht10_msgq, &sensor_data, K_NO_WAIT) == 0) {
        cached_temp = sensor_data.temperature;
        cached_humi = sensor_data.humidity;

        // A. 更新进度条 (圆环)
        lv_arc_set_value(meter_temp, (int)cached_temp);
        lv_arc_set_value(meter_humi, (int)cached_humi);

        // B. 【新增】更新中间的文字数值
        // 之前这里漏掉了，所以一直显示初始的 "0"
        lv_label_set_text_fmt(label_temp_val, "%d", (int)cached_temp);
        lv_label_set_text_fmt(label_humi_val, "%d", (int)cached_humi);
    }

    /* --- 3. IMU 数据占位 --- */
    // 更新右上角的文字
    icm20608_data_t imu_data;
    if (k_msgq_get(&imu_msgq, &imu_data, K_NO_WAIT) == 0) {
        lv_label_set_text_fmt(label_accel, 
            "IMU (N/A):\nAX: %.2f\nAY: %.2f\nAZ: %.2f\nTemp: %.1f", 
            (double)imu_data.accel_x, (double)imu_data.accel_y, (double)imu_data.accel_z,
            (double)imu_data.temp);
        /* 交互反馈：如果设备倾斜（Z轴分量减小），将边框设为红色 */
        lv_obj_t * imu_box = lv_obj_get_parent(label_accel);
        if (imu_data.accel_z < 0.5f) {
            lv_obj_set_style_border_color(imu_box, lv_palette_main(LV_PALETTE_RED), 0);
        } else {
            lv_obj_set_style_border_color(imu_box, lv_color_hex(0x00AEEF), 0);
        }
    } else {
        lv_label_set_text_fmt(label_accel, 
            "IMU (N/A):\nAX: 0.00\nAY: 0.00\nAZ: 1.00\nTemp: %.1f", 
            (double)cached_temp);
        // LOG_ERR("Failed to obtain IMU data!");
    }
}

/**
 * @brief 初始化按键硬件和 LVGL 输入设备
 */
void input_init(void)
{
    /* 初始化 GPIO */
    gpio_pin_configure_dt(&btn_up, GPIO_INPUT);
    gpio_pin_configure_dt(&btn_down, GPIO_INPUT);
    gpio_pin_configure_dt(&btn_left, GPIO_INPUT);
    gpio_pin_configure_dt(&btn_right, GPIO_INPUT);

    /* --- LVGL v9 新版注册方式 --- */
    
    /* 1. 创建一个输入设备对象，类型设为 KEYPAD */
    lv_indev_t * indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_KEYPAD);
    
    /* 2. 绑定读取回调函数 */
    lv_indev_set_read_cb(indev, keypad_read_cb);

    /* 3. 创建并关联组 (Group) */
    input_group = lv_group_create();
    lv_group_set_default(input_group);
    lv_indev_set_group(indev, input_group);
}

void display_thread_entry(void) 
{
    LOG_INF("Display Thread started");
    
    if (!device_is_ready(dev)) {
        LOG_ERR("Display device not ready!");
        return;
    }

    display_blanking_off(dev);
    k_msleep(120);

    backlight_init();
    backlight_set(100); // 开机先亮起来

    /* --- 初始化输入设备 --- */
    input_init();

    setup_pandora_dashboard();

    lv_timer_create(ui_timer_cb, 100, NULL);

    while (1) {
        lv_task_handler(); 
        k_msleep(30);      
    }
}

#define DISPLAY_STACK_SIZE 8192
#define DISPLAY_PRIORITY 10

K_THREAD_DEFINE(display_thread_tid, DISPLAY_STACK_SIZE, 
                display_thread_entry, NULL, NULL, NULL,
                DISPLAY_PRIORITY, 0, 0);