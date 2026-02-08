/*
 * icm20608.h
 * ICM-20608 传感器驱动头文件
 */

#ifndef ICM20608_DRIVER_H
#define ICM20608_DRIVER_H

#include <zephyr/types.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h> /* 新增：支持中断引脚 */

/* I2C 地址 (AD0 接地时为 0x68) */
#define ICM20608_ADDR               0x68
#define ICM20608_G_CHIP_ID          0xAF /* WHO_AM_I 默认值 */ 
#define ICM20608_D_CHIP_ID          0xAE

/* --- 寄存器地址定义 --- */
#define ICM20608_SMPLRT_DIV         0x19
#define ICM20608_CONFIG             0x1A
#define ICM20608_GYRO_CONFIG        0x1B
#define ICM20608_ACCEL_CONFIG       0x1C
#define ICM20608_ACCEL_CONFIG2      0x1D
#define ICM20608_INT_PIN_CFG        0x37 /* 中断引脚配置 */
#define ICM20608_INT_ENABLE         0x38 /* 中断使能 */
#define ICM20608_INT_STATUS         0x3A
#define ICM20608_ACCEL_XOUT_H       0x3B /* 数据读取起始地址 */
#define ICM20608_PWR_MGMT_1         0x6B
#define ICM20608_PWR_MGMT_2         0x6C
#define ICM20608_WHO_AM_I           0x75

/* --- 数据结构定义 --- */
typedef struct {
    float accel_x, accel_y, accel_z;
    float gyro_x, gyro_y, gyro_z;
    float temp;
} icm20608_data_t;

/* 【声明】外部消息队列：这行代码不产生实际队列，只是一个“入场券” */
/* 调用文件只要包含此头文件，就能合法地使用 imu_msgq */
extern struct k_msgq imu_msgq;

/* --- 驱动接口 API --- */

/**
 * @brief 模式一：初始化为轮询模式 (无需中断引脚)
 * @param i2c_spec I2C设备规范
 * @return 0 成功, 负数 失败
 */
int icm20608_init_polling(const struct i2c_dt_spec *i2c_spec);

/**
 * @brief 模式二：初始化为中断模式
 * @param i2c_spec  I2C设备规范
 * @param gpio_spec GPIO中断引脚规范
 * @param cb_data   GPIO回调结构体指针
 * @param handler   中断处理回调函数
 * @return 0 成功, 负数 失败
 */
int icm20608_init_interrupt(const struct i2c_dt_spec *i2c_spec,
                           const struct gpio_dt_spec *gpio_spec,
                           struct gpio_callback *cb_data,
                           gpio_callback_handler_t handler);

/**
 * @brief 突发读取传感器数据 (Accel + Gyro + Temp)
 * @param i2c_spec I2C设备规范
 * @param data 存储转换后物理数值的结构体
 * @return 0 成功, 负数 失败
 */
int icm20608_read_data(const struct i2c_dt_spec *i2c_spec, icm20608_data_t *data);

#endif /* ICM20608_DRIVER_H */