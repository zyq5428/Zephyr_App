/*
 * peripherals/ap3216c.c
 * AP3216C 传感器驱动的实现 (Zephyr 风格)
 */

#include <zephyr/logging/log.h>
#include <errno.h> 

#include "ap3216c.h"

// 启用日志记录
LOG_MODULE_REGISTER(AP3216C_DRV, LOG_LEVEL_INF);

/* --- 辅助 I2C 读写函数 (使用 Zephyr I2C API) --- */

/**
 * @brief 写入一个寄存器。
 */
static int write_reg(const struct i2c_dt_spec *i2c_spec, uint8_t reg, uint8_t data)
{
    uint8_t tx_buf[2] = {reg, data};

    if (!device_is_ready(i2c_spec->bus)) {
        LOG_ERR("I2C bus (%s) not ready.", i2c_spec->bus->name);
        return -ENODEV;
    }

    int ret = i2c_write_dt(i2c_spec, tx_buf, sizeof(tx_buf));
    if (ret != 0) {
        LOG_ERR("Failed to write reg 0x%x: %d", reg, ret);
    }
    return ret;
}

/**
 * @brief 读取一个或多个寄存器。
 */
static int read_regs(const struct i2c_dt_spec *i2c_spec, uint8_t reg, uint8_t len, uint8_t *buf)
{
    if (!device_is_ready(i2c_spec->bus)) {
        LOG_ERR("I2C bus (%s) not ready.", i2c_spec->bus->name);
        return -ENODEV;
    }

    // Zephyr I2C 写-读操作：先写寄存器地址，然后读数据
    int ret = i2c_write_read_dt(i2c_spec, &reg, 1, buf, len);
    if (ret != 0) {
        LOG_ERR("Failed to read reg 0x%x: %d", reg, ret);
    }
    return ret;
}


/**
 * @brief 从 LSB 和 MSB 寄存器读取一个 16 位（或以上）的值。
 */
static int32_t read_low_and_high(const struct i2c_dt_spec *i2c_spec, uint8_t reg)
{
    uint8_t buf[2];
    int ret;

    // 尝试读取 LSB (reg) 和 MSB (reg + 1)
    ret = read_regs(i2c_spec, reg, 2, buf);
    if (ret != 0) {
        return ret;
    }

    // 组合数据：buf[1] 是高字节，buf[0] 是低字节
    return (int32_t)((uint32_t)buf[1] << 8 | (uint32_t)buf[0]);
}


/* --- 驱动 API 实现 --- */

int ap3216c_reset_sensor(const struct i2c_dt_spec *i2c_spec)
{
    int ret;
    ret = write_reg(i2c_spec, AP3216C_SYS_CONFIGURATION_REG, AP3216C_MODE_SW_RESET);
    if (ret != 0) {
        return ret;
    }
    k_msleep(15); /* 软件复位后需要短暂延时 */
    return 0;
}

int ap3216c_set_mode(const struct i2c_dt_spec *i2c_spec, uint8_t mode)
{
    // 直接写入系统配置寄存器
    return write_reg(i2c_spec, AP3216C_SYS_CONFIGURATION_REG, mode);
}

int ap3216c_read_als_raw(const struct i2c_dt_spec *i2c_spec, uint16_t *als_data)
{
    int32_t data;

    data = read_low_and_high(i2c_spec, AP3216C_ALS_DATA_L_REG);

    if (data < 0) {
        return (int)data; // 返回错误码
    }

    // ALS 数据是 16 位的
    *als_data = (uint16_t)(data & 0xFFFF);

    return 0;
}

int ap3216c_read_ps_raw(const struct i2c_dt_spec *i2c_spec, uint16_t *ps_data)
{
    int32_t data;

    data = read_low_and_high(i2c_spec, AP3216C_PS_DATA_L_REG);

    if (data < 0) {
        return (int)data; // 返回错误码
    }

    uint32_t read_data = (uint32_t)data;

    // 提取 10 位 PS 数据
    *ps_data = (uint16_t)((read_data & 0x000f) | (((read_data >> 8) & 0x3f) << 4));

    return 0;
}

int ap3216c_set_param(const struct i2c_dt_spec *i2c_spec, ap3216c_cmd_t cmd, uint8_t value)
{
    int ret = 0;
    uint8_t args = 0;
    uint8_t reg_addr = (uint8_t)cmd;

    // 检查是否为简单的 1 字节写入 (基于 cmd 的地址范围)
    if ((reg_addr <= AP3216C_SYS_INT_CLEAR_MANNER_REG) ||
        (reg_addr >= AP3216C_ALS_CALIBRATION_REG && reg_addr <= AP3216C_ALS_THRESHOLD_HIGH_H_REG) ||
        (reg_addr >= AP3216C_PS_LED_DRIVER_REG && reg_addr <= AP3216C_PS_THRESHOLD_HIGH_H_REG && reg_addr != AP3216C_PS_CONFIGURATION_REG)) 
    {
        // 这些命令直接对应单个寄存器写入
        return write_reg(i2c_spec, reg_addr, value);
    }

    // 处理需要位操作的配置 (AP3216C_SYS_CONFIGURATION_REG, AP3216C_ALS_CONFIGURATION_REG, AP3216C_PS_CONFIGURATION_REG)
    switch (cmd)
    {
    case AP3216C_ALS_RANGE: // ALS Gain / Range
    {
        if (value > 0x3) return -EINVAL; // 0-3
        ret = read_regs(i2c_spec, AP3216C_ALS_CONFIGURATION_REG, 1, &args);
        if (ret != 0) return ret;
        args &= 0xcf; // 清除 Range/Gain 位 (5:4)
        args |= value << 4;
        ret = write_reg(i2c_spec, AP3216C_ALS_CONFIGURATION_REG, args);
        break;
    }
    case AP3216C_ALS_PERSIST:
    {
        if (value > 0x0f) return -EINVAL; // 0-15
        ret = read_regs(i2c_spec, AP3216C_ALS_CONFIGURATION_REG, 1, &args);
        if (ret != 0) return ret;
        args &= 0xf0; // 清除 Persist 位 (3:0)
        args |= value;
        ret = write_reg(i2c_spec, AP3216C_ALS_CONFIGURATION_REG, args);
        break;
    }
    case AP3216C_PS_GAIN:
    {
        if (value > 0x3) return -EINVAL; // 0-3
        ret = read_regs(i2c_spec, AP3216C_PS_CONFIGURATION_REG, 1, &args);
        if (ret != 0) return ret;
        args &= 0xf3; // 清除 Gain 位 (3:2)
        args |= value << 2;
        ret = write_reg(i2c_spec, AP3216C_PS_CONFIGURATION_REG, args);
        break;
    }
    case AP3216C_PS_PERSIST:
    {
        if (value > 0x3) return -EINVAL; // 0-3
        ret = read_regs(i2c_spec, AP3216C_PS_CONFIGURATION_REG, 1, &args);
        if (ret != 0) return ret;
        args &= 0xfc; // 清除 Persist 位 (1:0)
        args |= value;
        ret = write_reg(i2c_spec, AP3216C_PS_CONFIGURATION_REG, args);
        break;
    }
    default:
    {
        return -ENOTSUP; // 不支持的命令
    }
    }

    return ret;
}

int ap3216c_get_param(const struct i2c_dt_spec *i2c_spec, ap3216c_cmd_t cmd, uint8_t *value)
{
    int ret;
    uint8_t temp;
    uint8_t reg_addr = (uint8_t)cmd;

    // 检查是否为简单的 1 字节读取 (基于 cmd 的地址范围)
    if ((reg_addr <= AP3216C_SYS_INT_CLEAR_MANNER_REG) ||
        (reg_addr >= AP3216C_ALS_CALIBRATION_REG && reg_addr <= AP3216C_ALS_THRESHOLD_HIGH_H_REG) ||
        (reg_addr >= AP3216C_PS_LED_DRIVER_REG && reg_addr <= AP3216C_PS_THRESHOLD_HIGH_H_REG && reg_addr != AP3216C_PS_CONFIGURATION_REG)) 
    {
        // 这些命令直接对应单个寄存器读取
        return read_regs(i2c_spec, reg_addr, 1, value);
    }
    
    // 处理需要位操作的配置
    switch (cmd)
    {
    case AP3216C_ALS_RANGE: // ALS Gain / Range
    {
        ret = read_regs(i2c_spec, AP3216C_ALS_CONFIGURATION_REG, 1, &temp);
        if (ret != 0) return ret;
        *value = (temp >> 4) & 0x03; // 4:5 位
        return 0;
    }
    case AP3216C_ALS_PERSIST:
    {
        ret = read_regs(i2c_spec, AP3216C_ALS_CONFIGURATION_REG, 1, &temp);
        if (ret != 0) return ret;
        *value = temp & 0x0f; // 0:3 位
        return 0;
    }
    case AP3216C_PS_GAIN:
    {
        ret = read_regs(i2c_spec, AP3216C_PS_CONFIGURATION_REG, 1, &temp);
        if (ret != 0) return ret;
        *value = (temp >> 2) & 0x03; // 2:3 位
        return 0;
    }
    case AP3216C_PS_PERSIST:
    {
        ret = read_regs(i2c_spec, AP3216C_PS_CONFIGURATION_REG, 1, &temp);
        if (ret != 0) return ret;
        *value = temp & 0x03; // 0:1 位
        return 0;
    }
    default:
    {
        return -ENOTSUP;
    }
    }
}