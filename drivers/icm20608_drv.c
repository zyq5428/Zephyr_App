/*
 * icm20608_drv.c
 * ICM-20608 传感器底层驱动实现
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/logging/log.h>
#include "icm20608.h"

LOG_MODULE_REGISTER(ICM20608_DRV, LOG_LEVEL_INF);

/* 辅助函数：写寄存器 */
static int write_reg(const struct i2c_dt_spec *i2c_spec, uint8_t reg, uint8_t value)
{
    uint8_t buf[2] = {reg, value};
    return i2c_write_dt(i2c_spec, buf, sizeof(buf));
}

/* 内部基础初始化：验证ID并唤醒 */
static int icm20608_basic_setup(const struct i2c_dt_spec *i2c_spec)
{
    int ret;
    uint8_t id = 0;

    /* 检查 I2C 总线就绪 */
    if (!device_is_ready(i2c_spec->bus)) {
        LOG_ERR("I2C bus not ready");
        return -ENODEV;
    }

    /* 读取 WHO_AM_I 寄存器 */
    ret = i2c_write_read_dt(i2c_spec, (uint8_t[]){ICM20608_WHO_AM_I}, 1, &id, 1);
    if (ret != 0 || (id != ICM20608_G_CHIP_ID && id != ICM20608_D_CHIP_ID)) {
        LOG_ERR("Device ID mismatch: read 0x%02x, expect 0xaf or 0xae", id);
        return -EIO;
    }

    /* 复位设备 */
    write_reg(i2c_spec, ICM20608_PWR_MGMT_1, 0x80);
    k_msleep(100);

    /* 唤醒并设置时钟源 (Auto selects best clock) */
    write_reg(i2c_spec, ICM20608_PWR_MGMT_1, 0x01);     
    k_msleep(10);
    /* 启用加速度计和陀螺仪所有轴 */
    write_reg(i2c_spec, ICM20608_PWR_MGMT_2, 0x00);
    
    /* 关键：设置量程 */
    // ACCEL_CONFIG (0x1C): 写入 0x00 设置为 ±2g (灵敏度 16384 LSB/g)
    write_reg(i2c_spec, ICM20608_ACCEL_CONFIG, 0x00);
    // GYRO_CONFIG (0x1B): 写入 0x00 设置为 ±250 dps (灵敏度 131 LSB/dps)
    write_reg(i2c_spec, ICM20608_GYRO_CONFIG, 0x00);
    
    /* 设置采样率 (Sample Rate = Internal_Sample_Rate / (1 + SMPLRT_DIV)) */
    // 设置为 1kHz 采样 (1 + 0)
    write_reg(i2c_spec, ICM20608_SMPLRT_DIV, 0x09); // 1kHz / (1 + 9) = 100Hz 输出数据率

    LOG_DBG("ICM20608 Basic Setup Done (Range: ±2g, ±250dps)");
    k_msleep(100); // 等待稳定
    
    return 0;
}

/* --- 轮询模式初始化实现 --- */
int icm20608_init_polling(const struct i2c_dt_spec *i2c_spec)
{
    int ret = icm20608_basic_setup(i2c_spec);
    if (ret == 0) {
        LOG_INF("ICM20608 initialized in POLLING mode");
    }
    return ret;
}

/* --- 中断模式初始化实现 --- */
int icm20608_init_interrupt(const struct i2c_dt_spec *i2c_spec,
                           const struct gpio_dt_spec *gpio_spec,
                           struct gpio_callback *cb_data,
                           gpio_callback_handler_t handler)
{
    int ret;

    /* 基础硬件初始化 */
    ret = icm20608_basic_setup(i2c_spec);
    if (ret != 0) return ret;

    /* 配置中断引脚 (Register 55) */
    /* 0x30: 高电平有效，推挽输出，锁存模式，读操作自动清中断 */
    ret = write_reg(i2c_spec, ICM20608_INT_PIN_CFG, 0x30);
    
    /* 使能数据就绪中断 (Register 56) */
    /* 0x01: DATA_RDY_INT_EN 开启 */
    ret |= write_reg(i2c_spec, ICM20608_INT_ENABLE, 0x01);

    if (ret != 0) {
        LOG_ERR("Failed to write interrupt config registers");
        return -EIO;
    }

    /* 验证是否配置成功：读回寄存器值并打印 */
    uint8_t check_val = 0;
    i2c_write_read_dt(i2c_spec, (uint8_t[]){ICM20608_INT_ENABLE}, 1, &check_val, 1);
    LOG_INF("Verify INT_ENABLE: 0x%02x (Expect 0x01)", check_val);

    /* 配置 MCU 的 GPIO 中断 */
    if (!gpio_is_ready_dt(gpio_spec)) {
        LOG_ERR("GPIO device not ready");
        return -ENODEV;
    }

    gpio_pin_configure_dt(gpio_spec, GPIO_INPUT);
    gpio_init_callback(cb_data, handler, BIT(gpio_spec->pin));
    gpio_add_callback(gpio_spec->port, cb_data);
    gpio_pin_interrupt_configure_dt(gpio_spec, GPIO_INT_EDGE_TO_ACTIVE);

    uint8_t dummy;
    i2c_write_read_dt(i2c_spec, (uint8_t[]){ICM20608_INT_STATUS}, 1, &dummy, 1);
    LOG_INF("Initial INT_STATUS cleared: 0x%02x", dummy);

    LOG_INF("ICM20608 initialized in INTERRUPT mode");
    return 0;
}

/* 突发读取并转换数据 */
int icm20608_read_data(const struct i2c_dt_spec *i2c_spec, icm20608_data_t *data)
{
    uint8_t raw[14];
    int ret = i2c_burst_read_dt(i2c_spec, ICM20608_ACCEL_XOUT_H, raw, 14);
    if (ret != 0) return ret;

    /* 拼接 16 位有符号数 */
    int16_t ax = (int16_t)((raw[0] << 8) | raw[1]);
    int16_t ay = (int16_t)((raw[2] << 8) | raw[3]);
    int16_t az = (int16_t)((raw[4] << 8) | raw[5]);
    int16_t temp = (int16_t)((raw[6] << 8) | raw[7]);
    int16_t gx = (int16_t)((raw[8] << 8) | raw[9]);
    int16_t gy = (int16_t)((raw[10] << 8) | raw[11]);
    int16_t gz = (int16_t)((raw[12] << 8) | raw[13]);

    /* 物理量转换 (以默认量程为例) */
    data->accel_x = (float)ax / 16384.0f; // ±2g
    data->accel_y = (float)ay / 16384.0f;
    data->accel_z = (float)az / 16384.0f;
    data->temp = (float)temp / 326.8f + 25.0f;
    data->gyro_x = (float)gx / 131.0f;   // ±250dps
    data->gyro_y = (float)gy / 131.0f;
    data->gyro_z = (float)gz / 131.0f;

    return 0;
}