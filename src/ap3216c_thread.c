/*
 * application/src/ap3216c_thread.c
 * AP3216C 传感器数据读取线程的实现
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>
#include <errno.h> // 引入标准错误码
#include "ap3216c_thread.h"

// 启用日志记录
LOG_MODULE_REGISTER(AP3216C_TASK, LOG_LEVEL_INF);

// 通过设备树别名获取I2C设备的规范结构
// 别名 ap3216c-i2c 必须在 dts.overlay 中定义
static const struct i2c_dt_spec ap3216c_i2c_spec = I2C_DT_SPEC_GET(DT_ALIAS(ap3216c_i2c));

// --- 新增：全局变量，用于存储最新的光感值 ---
// 使用 volatile 关键字告诉编译器该变量可能在程序流程之外被修改
// volatile uint16_t g_als_raw_value = 0;

// 定义消息队列 ---
// 每一个数据包大小为 uint16_t，队列长度为 5
K_MSGQ_DEFINE(als_msgq, sizeof(uint16_t), 5, 4);

/**
 * @brief 初始化 AP3216C 传感器
 */
static int ap3216c_init(void)
{
    int ret;
    
    // 1. 复位传感器
    ret = ap3216c_reset_sensor(&ap3216c_i2c_spec);
    if (ret != 0) {
        LOG_ERR("Failed to reset AP3216C: %d", ret);
        return ret;
    }
    
    // 2. 切换到 ALS 和 PS 工作模式
    // 注意：这里使用 ap3216c_set_mode 替换了 ap3216c_init_mode
    ret = ap3216c_set_mode(&ap3216c_i2c_spec, AP3216C_MODE_ALS_AND_PS);
    if (ret != 0) {
        LOG_ERR("Failed to set mode AP3216C: %d", ret);
    } else {
        LOG_DBG("AP3216C initialized and set to ALS/PS mode successfully.");
    }

    // 3. (可选) 设置 ALS 增益为 AP3216C_ALS_GAIN1 (1x 增益，最大量程)
    ret = ap3216c_set_param(&ap3216c_i2c_spec, AP3216C_ALS_RANGE, AP3216C_ALS_RANGE_20661);
    if (ret != 0) {
        LOG_WRN("Failed to set ALS Gain: %d", ret);
    }


    return ret;
}

/* --- 线程入口点 --- */

void ap3216c_thread_entry(void *p1, void *p2, void *p3)
{
    int ret;
    uint16_t als_value = 0;
    const int SLEEP_MS = 1000;

    LOG_INF("AP3216C Thread started. I2C Bus: %s", ap3216c_i2c_spec.bus->name);

    // 1. 检查 I2C 设备是否就绪
    if (!device_is_ready(ap3216c_i2c_spec.bus)) {
        LOG_ERR("I2C Bus is not ready. Aborting thread.");
        return;
    }

    // 2. 初始化传感器
    ret = ap3216c_init();
    if (ret != 0) {
        LOG_ERR("Sensor initialization failed. Aborting thread. Error: %d", ret);
        return;
    }

    // 等待传感器启动 (至少 300ms)
    k_msleep(500);

    // 3. 周期性读取数据
    while (1) {
        ret = ap3216c_read_als_raw(&ap3216c_i2c_spec, &als_value);

        if (ret == 0) {
            // g_als_raw_value = als_value;
            // K_NO_WAIT 表示如果队列满了，不等待直接跳过
            // k_msgq_put(&als_msgq, &als_value, K_NO_WAIT);
            LOG_DBG("ALS Data: %u raw", als_value);
        } else {
            LOG_WRN("Failed to read ALS data: %d", ret);
        }

        k_msleep(SLEEP_MS);
    }
}


/* --- 线程定义和启动 --- */

// 定义栈空间、优先级和线程入口函数
#define AP3216C_STACK_SIZE 768
#define AP3216C_PRIORITY 7 // 较低的优先级，因为是周期性读取

K_THREAD_DEFINE(ap3216c_tid, AP3216C_STACK_SIZE, 
                ap3216c_thread_entry, NULL, NULL, NULL,
                AP3216C_PRIORITY, 0, 0);
