/*
 * switch_thread.c
 * 功能：识别四个按键的按下、松开、单击、长按，包含高级消抖与防重复逻辑
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

/* 注册日志模块 */
LOG_MODULE_REGISTER(SWITCH_TASK, LOG_LEVEL_INF);

/* 1. 参数定义 */
#define DEBOUNCE_TIME_MS    50    /* 软件消抖时间阈值 */
#define LONG_PRESS_TIME_MS  500   /* 长按判定时间阈值 */

/* 2. 获取设备树节点别名 (必须与 overlay 中的 aliases 对应) */
static const struct gpio_dt_spec sw_up    = GPIO_DT_SPEC_GET(DT_ALIAS(sw_up), gpios);
static const struct gpio_dt_spec sw_down  = GPIO_DT_SPEC_GET(DT_ALIAS(sw_down), gpios);
static const struct gpio_dt_spec sw_left  = GPIO_DT_SPEC_GET(DT_ALIAS(sw_left), gpios);
static const struct gpio_dt_spec sw_right = GPIO_DT_SPEC_GET(DT_ALIAS(sw_right), gpios);

/* 汇总按键指针数组，方便循环操作 */
static const struct gpio_dt_spec *buttons[] = {&sw_up, &sw_down, &sw_left, &sw_right};
static const char *button_names[] = {"UP", "DOWN", "LEFT", "RIGHT"};

/* 3. 按键状态维护结构体 */
struct button_state {
    struct gpio_callback cb;      /* Zephyr GPIO 回调结构体 */
    int64_t last_trigger_time;    /* 用于消抖检查的时间戳 */
    int64_t last_processed_time;  /* 关键：用于防重复注册的逻辑锁 */
    int64_t press_start_time;     /* 记录按下瞬间的时间戳 */
    bool is_pressed;              /* 当前逻辑电平是否处于按下状态 */
    bool long_press_triggered;    /* 标记长按是否已经触发过，防止重复打印 */
};

/* 定义 4 个按键的状态实例 */
static struct button_state btn_states[4];
/* 整个模块的初始化标志 */
static bool switch_is_initialized = false;

/* 4. 中断服务函数 (ISR) */
void button_isr(const struct device *port, struct gpio_callback *cb, uint32_t pins)
{
    /* 获取当前的系统运行时间 (毫秒) */
    int64_t now = k_uptime_get();
    
    /* 根据 pins 位掩码确定是哪个按键触发了中断 */
    int i;
    for (i = 0; i < 4; i++) {
        if (pins & BIT(buttons[i]->pin)) {
            break;
        }
    }
    if (i == 4) return; /* 未识别到匹配引脚则退出 */

    /* --- [防重入锁] --- 
     * 如果系统时间还没走，就再次进入了 ISR，说明链表中可能存在重复的回调结构体。
     * 这种情况 k_uptime_get() 会返回相同的值，我们直接丢弃。
     */
    if (now == btn_states[i].last_processed_time) {
        return;
    }
    btn_states[i].last_processed_time = now;

    /* --- [软件消抖] --- 
     * 两次中断触发的间隔如果小于 50ms，认为是机械抖动，不予处理。
     */
    if (now - btn_states[i].last_trigger_time < DEBOUNCE_TIME_MS) {
        return;
    }

    /* 获取当前的逻辑电平 (DT 已经根据 ACTIVE_HIGH/LOW 转换好了) 
     * val 为 1 代表有效触发（按下），0 代表无效（松开）
     */
    int val = gpio_pin_get_dt(buttons[i]);
    bool current_pressed = (val > 0);

    /* --- [状态突变检查] --- 
     * 因为配置了双边沿触发，我们必须确保当前读到的状态和我们记录的上一次状态不同。
     * 如果相同，说明是干扰或者已经处理过的逻辑，直接跳过。
     */
    if (current_pressed == btn_states[i].is_pressed) {
        return;
    }

    /* 正式更新最后一次有效触发的时间和状态 */
    btn_states[i].last_trigger_time = now;
    btn_states[i].is_pressed = current_pressed;

    if (current_pressed) {
        /* --- 处理按下事件 --- */
        btn_states[i].press_start_time = now;
        btn_states[i].long_press_triggered = false;
        LOG_INF("Button %s: [PRESSED]", button_names[i]);
    } else {
        /* --- 处理松开事件 --- */
        int64_t duration = now - btn_states[i].press_start_time;
        LOG_INF("Button %s: [RELEASED]", button_names[i]);

        /* 根据按下持续时间判定单击或长按 */
        if (duration < LONG_PRESS_TIME_MS) {
            LOG_INF("Button %s: [SINGLE CLICK] (Duration: %lld ms)", button_names[i], duration);
        } else {
            /* 只有没被实时线程触发过长按逻辑时，才在这里打印完成 */
            LOG_INF("Button %s: [LONG PRESS FINISHED] (Duration: %lld ms)", button_names[i], duration);
        }
    }
}

/* 5. 扫描线程入口 (用于实时识别长按，不需要等待松开) */
void switch_thread_entry(void *p1, void *p2, void *p3)
{
    /* 检查并执行初始化 */
    if (!switch_is_initialized) {
        int ret;
        for (int i = 0; i < 4; i++) {
            /* 硬件就绪检查 */
            if (!device_is_ready(buttons[i]->port)) {
                LOG_ERR("GPIO device %s not ready", button_names[i]);
                continue;
            }

            /* 1. 配置引脚为输入模式 */
            gpio_pin_configure_dt(buttons[i], GPIO_INPUT);

            /* 2. 配置硬件中断：双边沿触发 (Both Edges) */
            ret = gpio_pin_interrupt_configure_dt(buttons[i], GPIO_INT_EDGE_BOTH);
            if (ret != 0) {
                LOG_ERR("Failed to config interrupt on %s (%d)", button_names[i], ret);
                continue;
            }

            /* 3. 初始化并注册回调函数 */
            gpio_init_callback(&btn_states[i].cb, button_isr, BIT(buttons[i]->pin));
            gpio_add_callback(buttons[i]->port, &btn_states[i].cb);

            LOG_INF("Configured %s button (pin %d)", button_names[i], buttons[i]->pin);
        }
        switch_is_initialized = true;
        LOG_INF("All switches initialized successfully.");
    }

    /* 主循环：每 20ms 检查一次是否有按键处于长按阈值 */
    while (1) {
        int64_t now = k_uptime_get();
        for (int i = 0; i < 4; i++) {
            /* 如果按键正被按下，且还没触发过长按逻辑 */
            if (btn_states[i].is_pressed && !btn_states[i].long_press_triggered) {
                if (now - btn_states[i].press_start_time >= LONG_PRESS_TIME_MS) {
                    btn_states[i].long_press_triggered = true;
                    LOG_WRN("Button %s: [LONG PRESS DETECTED!]", button_names[i]);
                }
            }
        }
        k_msleep(20); 
    }
}

/* 6. 定义线程 */
#define SW_STACK_SIZE 1024
#define SW_PRIORITY 7

K_THREAD_DEFINE(sw_thread_id, SW_STACK_SIZE, 
                switch_thread_entry, NULL, NULL, NULL,
                SW_PRIORITY, 0, 0);