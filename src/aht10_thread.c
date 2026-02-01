/*
 * application/src/aht10_thread.c
 * AHT10 传感器数据读取线程的实现
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>
#include "aht10.h"

// 注册日志模块
LOG_MODULE_REGISTER(AHT10_TASK, LOG_LEVEL_INF);

// 通过设备树别名获取I2C设备的规范结构
static const struct i2c_dt_spec aht10_i2c_spec = I2C_DT_SPEC_GET(DT_NODELABEL(aht10_node));

// --- 消息队列定义 ---
// 队列项大小为 sizeof(aht10_data_t)，深度为 10
K_MSGQ_DEFINE(aht10_msgq, sizeof(aht10_data_t), 10, 4);

/* --- 线程入口函数 --- */
void aht10_thread_entry(void *p1, void *p2, void *p3)
{
    int ret;
    aht10_data_t sensor_data;
    const int SLEEP_MS = 2000; // 每2秒读取一次

    LOG_INF("AHT10 Thread started. I2C Bus: %s", aht10_i2c_spec.bus->name);

    // 1. 检查 I2C 总线是否就绪
    if (!device_is_ready(aht10_i2c_spec.bus)) {
        LOG_ERR("I2C Bus is not ready. Aborting AHT10 thread.");
        return;
    }

    // 2. 初始化传感器
    // AHT10 上电后需要一点时间稳定，建议先等待
    k_msleep(100); 

    ret = aht10_init_sensor(&aht10_i2c_spec);
    if (ret != 0) {
        LOG_ERR("AHT10 initialization failed: %d", ret);
        // 初始化失败是否退出线程取决于你的需求，这里选择继续尝试或直接退出
        return; 
    }

    // 3. 周期性循环
    while (1) {
        // 读取数据
        ret = aht10_read_data(&aht10_i2c_spec, &sensor_data);

        if (ret == 0) {
            // 打印调试信息 (float 在 Zephyr 中打印可能需要配置 CONFIG_CBPRINTF_FP_SUPPORT=y)
            // 如果为了保险，可以强转成整数打印部分小数
            LOG_DBG("AHT10: Temp=%.2f C, Humi=%.2f %%RH", 
                    (double)sensor_data.temperature, (double)sensor_data.humidity);

            // 将数据放入消息队列供其他模块使用
            // K_NO_WAIT: 如果队列满了，丢弃旧数据或直接跳过，不阻塞线程
            k_msgq_put(&aht10_msgq, &sensor_data, K_NO_WAIT);
        } else {
            LOG_WRN("Failed to read AHT10: %d", ret);
            
            // 如果连续读取失败，可能需要尝试重新复位或初始化
            // aht10_soft_reset(&aht10_i2c_spec);
            // k_msleep(20);
            // aht10_init_sensor(&aht10_i2c_spec);
        }

        k_msleep(SLEEP_MS);
    }
}

/* --- 线程定义 --- */
#define AHT10_STACK_SIZE 1024
#define AHT10_PRIORITY 7

K_THREAD_DEFINE(aht10_tid, AHT10_STACK_SIZE,
                aht10_thread_entry, NULL, NULL, NULL,
                AHT10_PRIORITY, 0, 0);