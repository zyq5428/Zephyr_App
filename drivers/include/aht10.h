/*
 * peripherals/include/aht10.h
 * AHT10 温湿度传感器驱动头文件 (Zephyr 风格)
 */

#ifndef AHT10_DRIVER_H
#define AHT10_DRIVER_H

#include <zephyr/types.h>
#include <zephyr/drivers/i2c.h>

/* I2C 地址 (AHT10 固定地址) */
#define AHT10_ADDR                  0x38

/* AHT10 命令字 */
#define AHT10_CMD_INIT              0xE1    /* 初始化/校准命令 */
#define AHT10_CMD_TRIGGER           0xAC    /* 触发测量命令 */
#define AHT10_CMD_SOFT_RESET        0xBA    /* 软复位命令 */

/* AHT10 状态位掩码 */
#define AHT10_STATUS_BUSY           (1 << 7) /* Bit7: 忙指示 (1=忙, 0=空闲) */
#define AHT10_STATUS_CALIBRATED     (1 << 3) /* Bit3: 校准使能位 (1=已校准) */

/**
 * @brief AHT10 数据结构体，用于存储转换后的温湿度
 */
typedef struct {
    float temperature;  /* 温度，单位：摄氏度 (°C) */
    float humidity;     /* 湿度，单位：百分比 (%RH) */
} aht10_data_t;

/* --- Zephyr 风格的驱动 API --- */

/**
 * @brief 初始化 AHT10 传感器 (发送校准命令)
 * * @param i2c_spec I2C 设备描述结构体指针
 * @return int 0 表示成功，负数表示错误码
 */
int aht10_init_sensor(const struct i2c_dt_spec *i2c_spec);

/**
 * @brief 读取温湿度数据
 * * 此函数会发送触发命令，等待转换完成，然后读取并计算数据。
 * * @param i2c_spec I2C 设备描述结构体指针
 * @param data 用于接收结果的结构体指针
 * @return int 0 表示成功，负数表示错误码
 */
int aht10_read_data(const struct i2c_dt_spec *i2c_spec, aht10_data_t *data);

/**
 * @brief 执行软复位
 * * @param i2c_spec I2C 设备描述结构体指针
 * @return int 0 表示成功，负数表示错误码
 */
int aht10_soft_reset(const struct i2c_dt_spec *i2c_spec);

#endif /* AHT10_DRIVER_H */