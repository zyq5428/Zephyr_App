/*
 * application/src/ap3216c_thread.c
 * AP3216C 传感器数据读取线程的实现
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>

// 包含我们自己的头文件
#include "ap3216c_thread.h"

// 启用日志记录
LOG_MODULE_REGISTER(AP3216C_TASK, LOG_LEVEL_INF);

// 通过设备树别名获取I2C设备的规范结构
// 别名 ap3216c-i2c 必须在 dts.overlay 中定义
static const struct i2c_dt_spec ap3216c_i2c_spec = I2C_DT_SPEC_GET(DT_ALIAS(ap3216c_i2c));

// AP3216C 寄存器地址定义 (请根据手册核对)
#define AP3216C_SYSTEM_MODE_REG 0x00 // 系统模式配置寄存器
#define AP3216C_ALS_DATA_LOW    0x0C // ALS环境光数据低字节寄存器
#define AP3216C_ALS_DATA_HIGH   0x0D // ALS环境光数据高字节寄存器
#define AP3216C_PS_DATA_LOW     0x0E // 接近传感器数据低字节寄存器

/* --- 辅助函数 --- */

/**
 * @brief 初始化 AP3216C 传感器
 * * 将传感器设置为 ALS/PS 模式 (例如：写入 0x04 到 0x00 寄存器)
 */
static int ap3216c_init(void)
{
    int ret;
    // 写入配置：寄存器地址 (0x00) + 配置值 (0x04)
    uint8_t tx_buf[] = {AP3216C_SYSTEM_MODE_REG, 0x04};

    if (!device_is_ready(ap3216c_i2c_spec.bus)) {
        LOG_ERR("I2C bus (%s) not ready.", ap3216c_i2c_spec.bus->name);
        return -ENODEV;
    }

    ret = i2c_write_dt(&ap3216c_i2c_spec, tx_buf, sizeof(tx_buf));

    if (ret != 0) {
        LOG_ERR("Failed to init AP3216C (Addr: 0x%x): %d",
                ap3216c_i2c_spec.addr, ret);
    } else {
        LOG_INF("AP3216C initialized successfully.");
    }

    return ret;
}

/**
 * @brief 从 AP3216C 读取 ALS（环境光）数据
 */
static int read_ap3216c_als_data(uint16_t *als_data)
{
    int ret;
    uint8_t als_buffer[2];
    uint8_t als_reg_addr = AP3216C_ALS_DATA_LOW;

    // 执行 I2C 写-读操作：写入寄存器地址 0x0C，然后读取 2 字节数据
    ret = i2c_write_read_dt(&ap3216c_i2c_spec, 
                            &als_reg_addr, 1, 
                            als_buffer, 2);

    if (ret != 0) {
        return ret;
    }

    // 组合数据：假设 als_buffer[0] 是低字节 (0x0C)，als_buffer[1] 是高字节 (0x0D)
    *als_data = (uint16_t)(als_buffer[1] << 8) | als_buffer[0];

    return 0;
}


/* --- 线程入口点 --- */

void ap3216c_thread_entry(void *p1, void *p2, void *p3)
{
    int ret;
    uint16_t als_value = 0;
    const int SLEEP_MS = 1000;

    LOG_INF("AP3216C Thread started. I2C Bus: %s", ap3216c_i2c_spec.bus->name);

    // 1. 检查设备是否就绪
    if (!device_is_ready(ap3216c_i2c_spec.bus)) {
        LOG_ERR("I2C Bus is not ready. Aborting thread.");
        return;
    }

    // 2. 初始化传感器
    ret = ap3216c_init();
    if (ret != 0) {
        LOG_ERR("Sensor initialization failed. Aborting thread.");
        return;
    }

    // 等待传感器启动 (至少 300ms)
    k_msleep(500);

    // 3. 周期性读取数据
    while (1) {
        ret = read_ap3216c_als_data(&als_value);

        if (ret == 0) {
            LOG_INF("ALS Data: %u lux (Raw)", als_value);
        } else {
            LOG_WRN("Failed to read ALS data: %d", ret);
        }

        k_msleep(SLEEP_MS);
    }
}


/* --- 线程定义和启动 --- */

// 定义栈空间、优先级和线程入口函数
#define AP3216C_STACK_SIZE 1024
#define AP3216C_PRIORITY 7 // 较低的优先级，因为是周期性读取

K_THREAD_DEFINE(ap3216c_tid, AP3216C_STACK_SIZE, 
                ap3216c_thread_entry, NULL, NULL, NULL,
                AP3216C_PRIORITY, 0, 0);


void start_ap3216c_thread(void)
{
    // 线程已在 K_THREAD_DEFINE 中自动启动，此函数仅用于提供统一的启动接口
    LOG_DBG("AP3216C thread defined and started.");
}