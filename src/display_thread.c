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
static lv_obj_t * label_temp_val; // 温度数值文字句柄
static lv_obj_t * label_humi_val; // 湿度数值文字句柄

static lv_obj_t * label_accel;   // IMU文字
static lv_obj_t * label_lux;     // 光照文字
static lv_obj_t * chart_light;   // 光照图表
static lv_chart_series_t * ser_lux; 

/* --- 新增：小球模式变量 --- */
static lv_obj_t *imu_ball = NULL;    // 小球对象句柄
static bool is_ball_active = false;  // 标记是否处于加速度计小球模拟模式
static lv_obj_t *imu_cont_global;    // 记录 IMU 容器句柄，方便定时器识别
/* 用于平滑移动的影子坐标 */
static float ball_current_x = 110.0f;
static float ball_current_y = 110.0f;

// 缓存数据
static uint16_t cached_lux = 0;
static float cached_temp = 0.0f;
static float cached_humi = 0.0f;

static bool is_full_screen = false;  // 记录当前是否处于全屏状态
static lv_point_t old_pos;           // 记录对象的原始位置
static lv_area_t old_size;           // 记录对象的原始尺寸

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
 * @brief 隐藏/显示屏幕上除当前对象外的所有内容
 */
static void set_main_ui_visible(lv_obj_t * current_obj, bool visible) {
    lv_obj_t * screen = lv_scr_act();
    uint32_t child_cnt = lv_obj_get_child_cnt(screen);
    
    for(uint32_t i = 0; i < child_cnt; i++) {
        lv_obj_t * child = lv_obj_get_child(screen, i);
        // 关键：不要隐藏正在放大的那个对象，也不要隐藏背景/顶层
        if (child != current_obj) {
            if(visible) 
                lv_obj_clear_flag(child, LV_OBJ_FLAG_HIDDEN);
            else 
                lv_obj_add_flag(child, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/**
 * @brief 通用传感器对象交互处理器
 * 支持：颜色焦点提示、全屏放大、全屏缩小、自动隐藏干扰元素
 */
static void sensor_common_event_handler(lv_event_t * e)
{
    lv_obj_t * obj = lv_event_get_target(e);
    lv_event_code_t code = lv_event_get_code(e);

    /* 1. 焦点视觉反馈 (保持原样) */
    if (!is_full_screen) {
        if (code == LV_EVENT_FOCUSED) {
            lv_obj_set_style_outline_width(obj, 0, LV_STATE_FOCUS_KEY);
            // 这里判断是否是 IMU 容器（它不是 arc，所以逻辑略有不同）
            if(lv_obj_check_type(obj, &lv_arc_class)) {
                lv_obj_set_style_arc_color(obj, lv_palette_main(LV_PALETTE_BLUE), LV_PART_INDICATOR);
            } else {
                lv_obj_set_style_border_color(obj, lv_palette_main(LV_PALETTE_BLUE), 0);
            }
        } else if (code == LV_EVENT_DEFOCUSED) {
            if(lv_obj_check_type(obj, &lv_arc_class)) {
                lv_obj_set_style_arc_color(obj, lv_palette_lighten(LV_PALETTE_GREY, 1), LV_PART_INDICATOR);
            } else {
                lv_obj_set_style_border_color(obj, lv_color_hex(0x00AEEF), 0);
            }
        }
    }

    /* 2. 按键交互逻辑 */
    if (code == LV_EVENT_KEY) {
        uint32_t key = lv_indev_get_key(lv_indev_get_act());

        // --- 【ENTER】: 进入全屏/小球模式 ---
        if (key == LV_KEY_ENTER && !is_full_screen) {
            is_full_screen = true;

            // 停止可能存在的残留动画
            lv_anim_del(obj, NULL);

            // 核心：在改变任何属性前，精准记录原始位置和尺寸
            old_pos.x = lv_obj_get_x(obj);
            old_pos.y = lv_obj_get_y(obj);
            old_size.x1 = lv_obj_get_width(obj);
            old_size.y1 = lv_obj_get_height(obj);

            set_main_ui_visible(obj, false);
            lv_obj_move_foreground(obj); // 将容器移到最前
            
            // 1. 先设置容器为全屏黑色背景
            lv_obj_set_size(obj, 240, 240);
            lv_obj_set_pos(obj, 0, 0);
            lv_obj_set_style_bg_color(obj, lv_color_hex(0x000000), 0);
            lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);

            // 2. 如果是 IMU 容器，处理小球
            if (obj == imu_cont_global) {
                is_ball_active = true;
                lv_obj_add_flag(label_accel, LV_OBJ_FLAG_HIDDEN);
                
                if (imu_ball == NULL) {
                    imu_ball = lv_obj_create(obj);
                    lv_obj_set_size(imu_ball, 20, 20);
                    lv_obj_set_style_radius(imu_ball, LV_RADIUS_CIRCLE, 0);
                    lv_obj_set_style_bg_color(imu_ball, lv_palette_main(LV_PALETTE_RED), 0);
                    lv_obj_set_style_shadow_width(imu_ball, 15, 0);
                    lv_obj_set_style_shadow_color(imu_ball, lv_palette_main(LV_PALETTE_RED), 0);
                }
                
                // 【核心修复】：显式清除隐藏标志并将其移至子对象的最前端
                lv_obj_clear_flag(imu_ball, LV_OBJ_FLAG_HIDDEN);
                lv_obj_move_foreground(imu_ball); // 确保小球在黑色背景之上
                lv_obj_set_pos(imu_ball, 110, 110); 
            }
            
            // 执行淡入动画
            lv_obj_set_style_opa(obj, LV_OPA_TRANSP, 0);
            lv_anim_t a;
            lv_anim_init(&a);
            lv_anim_set_var(&a, obj);
            lv_anim_set_time(&a, 200);
            lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_style_opa);
            lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
            lv_anim_start(&a);
        } 

        // --- 【ESC】: 退出全屏/还原数值模式 ---
        else if (key == LV_KEY_ESC && is_full_screen) {
            is_full_screen = false;

            // 彻底停止所有动画和属性修改
            lv_anim_del(obj, NULL);

            /* --- 关键修复：强制重置对齐方式为左上角 --- */
            /* 这样 old_pos.x 和 old_pos.y 的绝对像素值才会生效 */
            lv_obj_set_align(obj, LV_ALIGN_TOP_LEFT);
            
            if (obj == imu_cont_global) {
                is_ball_active = false;
                lv_obj_add_flag(imu_ball, LV_OBJ_FLAG_HIDDEN);    // 隐藏小球
                lv_obj_clear_flag(label_accel, LV_OBJ_FLAG_HIDDEN); // 恢复文字显示
            }

            // 1. 暴力清除进入时设置的本地属性（这一步最重要，防止样式污染坐标计算）
            lv_obj_remove_local_style_prop(obj, LV_STYLE_BG_OPA, 0);
            lv_obj_remove_local_style_prop(obj, LV_STYLE_BG_COLOR, 0);
            lv_obj_remove_local_style_prop(obj, LV_STYLE_OPA, 0);

            // 2. 严格还原：先还原尺寸，再还原坐标
            lv_obj_set_size(obj, (int32_t)old_size.x1, (int32_t)old_size.y1);
            lv_obj_set_pos(obj, old_pos.x, old_pos.y);
            
            // 3. 显式设回主界面半透明/透明样式
            lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
            set_main_ui_visible(obj, true);

            // 4. 强制通知 LVGL 整个屏幕已经“脏了”，需要重新刷新全部像素
            lv_obj_invalidate(lv_scr_act());
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
    imu_cont_global = imu_cont; // 保存到全局变量
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

    /* --- 将四个对象加入 Group 并绑定事件 --- */
    lv_obj_t * objs[] = {meter_temp, meter_humi, imu_cont, chart_light};
    
    for(int i = 0; i < 4; i++) {
        lv_obj_add_flag(objs[i], LV_OBJ_FLAG_CLICKABLE);
        lv_group_add_obj(input_group, objs[i]);
        lv_obj_add_event_cb(objs[i], sensor_common_event_handler, LV_EVENT_ALL, NULL);
        // 初始状态全部设为灰色（代表未选中）
        lv_obj_set_style_arc_color(objs[i], lv_palette_lighten(LV_PALETTE_GREY, 1), LV_PART_INDICATOR);
        
        // 彻底取消每个对象的默认方框
        lv_obj_set_style_outline_width(objs[i], 0, LV_STATE_FOCUS_KEY);
        lv_obj_set_style_border_width(objs[i], 0, LV_STATE_FOCUS_KEY);
    }

    /* 默认聚焦在第一个圆环 */
    lv_group_focus_obj(meter_temp);
}

/**
 * @brief LVGL 键盘读取回调函数
 */
static void keypad_read_cb(lv_indev_t * indev, lv_indev_data_t * data)
{
    data->state = LV_INDEV_STATE_REL; 

    /* 下=确认, 上=返回, 左=上一个, 右=下一个 */
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
        data->key = LV_KEY_PREV;
        LOG_INF("Left Key Pressed!");
    } else if (gpio_pin_get_dt(&btn_right) > 0) {
        data->state = LV_INDEV_STATE_PR;
        data->key = LV_KEY_NEXT;
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
        
        if (is_ball_active && imu_ball) {
            /* * 小球物理映射算法修复：
             * 1. 屏幕中心是 (120, 120)。
             * 2. 小球大小是 20x20，所以小球中心对准屏幕中心时，其左上角坐标应为 (110, 110)。
             * 3. 加速度计 X 轴对应屏幕 Y 轴，Y 轴对应屏幕 X 轴（取决于你的安装方向）。
             */
            
            // 1. 设定灵敏度和滤波系数
            const float sensitivity = 100.0f; 
            const float filter_gain = 0.15f; // 滤波系数(0.0~1.0)，越小越平滑，0.1~0.2 效果最佳

            // 2. 计算目标位置 (针对你的板子映射：accel_x->x, accel_y->y)
            // 如果方向反了，请在 imu_data 前加负号
            float target_x = 110.0f + (imu_data.accel_x * sensitivity);
            float target_y = 110.0f - (imu_data.accel_y * sensitivity);

            // 3. 一阶滤波：让当前位置向目标位置“平滑靠拢”
            ball_current_x = (ball_current_x * (1.0f - filter_gain)) + (target_x * filter_gain);
            ball_current_y = (ball_current_y * (1.0f - filter_gain)) + (target_y * filter_gain);

            // 4. 边界检查
            if(ball_current_x < 0) ball_current_x = 0;
            if(ball_current_x > 220) ball_current_x = 220;
            if(ball_current_y < 0) ball_current_y = 0;
            if(ball_current_y > 220) ball_current_y = 220;

            // 5. 应用坐标 (转换为整数)
            lv_obj_set_pos(imu_ball, (int16_t)ball_current_x, (int16_t)ball_current_y);
        } 
        else {
            /* 标准数值模式 (重置平滑坐标为中心，防止下次进入时闪现) */
            ball_current_x = 110.0f;
            ball_current_y = 110.0f;

            lv_label_set_text_fmt(label_accel, 
                "IMU Data:\nAX: %.2f\nAY: %.2f\nAZ: %.2f\nTemp: %.1f", 
                (double)imu_data.accel_x, (double)imu_data.accel_y, (double)imu_data.accel_z,
                (double)imu_data.temp);

            if (imu_data.accel_z < 0.5f) {
                lv_obj_set_style_border_color(imu_cont_global, lv_palette_main(LV_PALETTE_RED), 0);
            } else {
                lv_obj_set_style_border_color(imu_cont_global, lv_color_hex(0x00AEEF), 0);
            }
        }
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