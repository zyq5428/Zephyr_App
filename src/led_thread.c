#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/logging/log.h>
#include "led_thread.h"

// 启用日志记录
LOG_MODULE_REGISTER(LED_TASK, LOG_LEVEL_INF);

/* ---------------- 配置参数 ---------------- */
#define TOTAL_STEPS         10    // 1秒内总共调整10次
#define TOTAL_TIME_MS       1000  // 1000 毫秒
#define STEP_DELAY_MS       (TOTAL_TIME_MS / TOTAL_STEPS) // 每次调整间隔 100ms

/* GPIO_DT_SPEC_GET(node_id, property)： 这是Zephyr提供的另一个宏，
* 用于从指定的设备节点ID (LED1_NODE) 中提取名为 property 的属性值，
* 并将其封装成 struct gpio_dt_spec。
* 这里的 gpios 是一个标准的设备树属性名。
*/
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
// 从设备树中获取定义的 PWM LED 节点
static const struct pwm_dt_spec pwm_led = PWM_DT_SPEC_GET(DT_ALIAS(pwm_led0));

void led_thread_entry(void *p1, void *p2, void *p3)
{
    // ... 在这里初始化您的 LED 设备 ...
    LOG_INF("LED Thread started");

    int ret;
	uint32_t step_count = 0;
    uint32_t pulse_width = 0;
    uint8_t brightness_percent = 0;

	/* 初始化 GPIO (LED) */
    if (!gpio_is_ready_dt(&led)) {
        LOG_ERR("GPIO device not ready");
        return;
    }

    /* 检查 PWM 设备 */
    if (!pwm_is_ready_dt(&pwm_led)) {
        LOG_ERR("PWM device not ready");
        return;
    }

	ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
	if (ret < 0) {
		LOG_ERR("Error: Failed to configure LED pin (ret=%d)", ret);
        while(1) { return; } 
	}
	k_msleep(1000); // 等待 1 秒
	gpio_pin_toggle_dt(&led);
	LOG_DBG("GPIO LED Toggled!");


    while (1) {
		/* --- 1. 计算亮度 (0-100-0) --- */
        // step_count 从 0 增加到 19
        if (step_count < 5) {
            // 前 10 次：从 0% 增加到 90%
            brightness_percent = step_count * 10;
        } else {
            // 后 10 次：从 100% 减少到 1%
            brightness_percent = 100 - step_count * 10;
        }

        /* --- 2. 设置 PWM 亮度 --- */
        // 计算脉冲宽度: 周期 * 百分比 / 100
        pulse_width = (pwm_led.period / 100) * brightness_percent;
        
        ret = pwm_set_dt(&pwm_led, pwm_led.period, pulse_width);
        if (ret < 0) {
            LOG_ERR("Failed to set PWM: %d", ret);
        } else {
			LOG_DBG("PWM set: Brightness %d%%, Pulse Width %u ns",
					brightness_percent, pulse_width);
		}

		/* --- 3. 每 1 分钟切换一次 GPIO LED 状态 --- */
        // 每 20 次步进（即一秒钟）翻转一次
        if (step_count == 0) {
            gpio_pin_toggle_dt(&led);
            LOG_DBG("GPIO LED Toggled, Restarting PWM Cycle");
        }

        /* --- 4. 步进控制 --- */
        step_count++;
        if (step_count >= TOTAL_STEPS) {
            step_count = 0; // 重置计数器，开始下一个一分钟
        }

        /* 休眠 300ms */
        k_msleep(STEP_DELAY_MS);
    }
}

// 线程栈和定义
#define LED_STACK_SIZE 1024
#define LED_PRIORITY 15

K_THREAD_DEFINE(led_tid, LED_STACK_SIZE, 
                led_thread_entry, NULL, NULL, NULL,
                LED_PRIORITY, 0, 0);
