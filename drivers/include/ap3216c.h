/*
 * peripherals/include/ap3216c.h
 * AP3216C 传感器驱动的头文件 (Zephyr 风格, 完整参数定义)
 */

#ifndef AP3216C_DRIVER_H
#define AP3216C_DRIVER_H

#include <zephyr/types.h>
#include <zephyr/drivers/i2c.h> // 引入 Zephyr I2C 结构体

// I2C 地址
#define AP3216C_ADDR 0x1e /* 7-bit address */

// --- AP3216C 寄存器定义 ---
// System Register
#define AP3216C_SYS_CONFIGURATION_REG       0x00 // Default: 0x03
#define AP3216C_SYS_INT_STATUS_REG          0x01
#define AP3216C_SYS_INT_CLEAR_MANNER_REG    0x02

// Data Register (Read Only)
#define AP3216C_IR_DATA_L_REG               0x0A
#define AP3216C_IR_DATA_H_REG               0x0B
#define AP3216C_ALS_DATA_L_REG              0x0C
#define AP3216C_ALS_DATA_H_REG              0x0D
#define AP3216C_PS_DATA_L_REG               0x0E
#define AP3216C_PS_DATA_H_REG               0x0F

// ALS Register (Configuration) : Ambient Light Sensing (环境光感应/传感器)
#define AP3216C_ALS_CONFIGURATION_REG       0x10 // Range/Gain 5:4, Persist 3:0
#define AP3216C_ALS_CALIBRATION_REG         0x19
#define AP3216C_ALS_THRESHOLD_LOW_L_REG     0x1A // Low Threshold L (bit 7:0)
#define AP3216C_ALS_THRESHOLD_LOW_H_REG     0x1B // Low Threshold H (bit 15:8)
#define AP3216C_ALS_THRESHOLD_HIGH_L_REG    0x1C // High Threshold L (bit 7:0)
#define AP3216C_ALS_THRESHOLD_HIGH_H_REG    0x1D // High Threshold H (bit 15:8)

// PS Register (Configuration) : Proximity Sensing (接近感应/传感器)
#define AP3216C_PS_CONFIGURATION_REG        0x20 // PS Gain 3:2, PS Persist 1:0
#define AP3216C_PS_LED_DRIVER_REG           0x21
#define AP3216C_PS_INT_FORM_REG             0x22
#define AP3216C_PS_MEAN_TIME_REG            0x23
#define AP3216C_PS_LED_WAITING_TIME_REG     0x24
#define AP3216C_PS_CALIBRATION_L_REG        0x28
#define AP3216C_PS_CALIBRATION_H_REG        0x29
#define AP3216C_PS_THRESHOLD_LOW_L_REG      0x2A // Low Threshold L (bit 7:0)
#define AP3216C_PS_THRESHOLD_LOW_H_REG      0x2B // Low Threshold H (bit 9:8)
#define AP3216C_PS_THRESHOLD_HIGH_L_REG     0x2C // High Threshold L (bit 7:0)
#define AP3216C_PS_THRESHOLD_HIGH_H_REG     0x2D // High Threshold H (bit 9:8)


// --- 枚举定义 ---

/**
 * @brief AP3216C 工作模式
 */
enum ap3216c_mode_value
{
    AP3216C_MODE_POWER_DOWN = 0,      // Power down (Default)
    AP3216C_MODE_ALS = 1,             // ALS function active
    AP3216C_MODE_PS = 2,              // PS+IR function active
    AP3216C_MODE_ALS_AND_PS = 3,      // ALS and PS+IR functions active
    AP3216C_MODE_SW_RESET = 4,        // SW reset
    AP3216C_MODE_ALS_ONCE = 5,        // ALS function once
    AP3216C_MODE_PS_ONCE = 6,         // PS+IR function once
    AP3216C_MODE_ALS_AND_PS_ONCE = 7, // ALS and PS+IR functions once
};

/**
 * @brief 中断清除方式 (AP3216C_SYS_INT_CLEAR_MANNER_REG)
 */
enum ap3216c_int_clear_manner
{
    AP3216C_INT_CLEAR_MANNER_BY_READING = 0, // INT is automatically cleared by reading data registers (Default)
    AP3216C_ALS_CLEAR_MANNER_BY_SOFTWARE = 1, // Software clear after writing 1 into address 0x01 each bit
};

/**
 * @brief ALS 测量量程/范围 (AP3216C_ALS_CONFIGURATION_REG 5:4)
 * 注意：根据 AP3216C_ALS_CONFIGURATION_REG，ALS Gain 和 ALS Range 实际上是同一个位域 (Range 5:4)。
 * 这里使用 Range 作为命名，因为它直接影响最大量程。
 */
enum als_range
{
    AP3216C_ALS_RANGE_20661 = 0, // Max range: 20661 Lux (Gain 1x)
    AP3216C_ALS_RANGE_5162 = 1,  // Max range: 5162 Lux (Gain 4x)
    AP3216C_ALS_RANGE_1291 = 2,  // Max range: 1291 Lux (Gain 16x)
    AP3216C_ALS_RANGE_323 = 3,   // Max range: 323 Lux (Gain 64x)
};
typedef enum als_range als_range_t;

/**
 * @brief ALS 增益/量程 (AP3216C_ALS_CONFIGURATION_REG 5:4)
 * 增益值 0-3 对应不同的 Lux 量程。
 */
enum als_gain
{
    AP3216C_ALS_GAIN1 = 0, // 增益 1x，最大量程 20661 Lux
    AP3216C_ALS_GAIN2 = 1, // 增益 4x，最大量程 5162 Lux
    AP3216C_ALS_GAIN4 = 2, // 增益 16x，最大量程 1291 Lux
    AP3216C_ALS_GAIN8 = 3, // 增益 64x，最大量程 323 Lux
};
typedef enum als_gain als_gain_t;


/**
 * @brief AP3216C 可配置参数命令
 */
enum ap3216c_cmd
{
    AP3216C_SYSTEM_MODE,          //system  Configuration(Default : 000)
    AP3216C_INT_PARAM,            //INT Clear Manner(Default : 0)
    AP3216C_ALS_RANGE,            //ALS dynamic range(Default : 00)
    AP3216C_ALS_PERSIST,          //ALS persist(Default : 0000)
    AP3216C_ALS_CALIBRATION,      //ALS window loss calibration(Default : 0x40)
    AP3216C_ALS_LOW_THRESHOLD_L,  //Lower byte of low interrupt threshold for ALS(Default : 0x00)
    AP3216C_ALS_LOW_THRESHOLD_H,  //Higher byte of low interrupt threshold for ALS(Default : 0x00)
    AP3216C_ALS_HIGH_THRESHOLD_L, //Lower byte of high interrupt threshold for ALS (Default : 0xFF)
    AP3216C_ALS_HIGH_THRESHOLD_H, //Higher byte of high interrupt threshold for ALS(Default : 0xFF)
    AP3216C_PS_INTEGRATED_TIME,   //PS or IR Integrated time select(Default : 0000)
    AP3216C_PS_GAIN,              //PS gain (Default : 01)
    AP3216C_PS_PERSIST,           //Interrupt filter(Default : 01)
    AP3216C_PS_LED_CONTROL,       //LED pulse(Default : 01)
    AP3216C_PS_LED_DRIVER_RATIO,  //LED driver ratio(Default : 11)
    AP3216C_PS_INT_MODE,          //PS INT Mode(Default : 0x01)
    AP3216C_PS_MEAN_TIME,         //PS mean time(Default : 0x00)
    AP3216C_PS_WAITING_TIME,      //PS LED Waiting(Default : 0x00)
    AP3216C_PS_CALIBRATION_L,     //PS Calibration L(Default : 0x00)
    AP3216C_PS_CALIBRATION_H,     //PS Calibration H(Default : 0x00)
    AP3216C_PS_LOW_THRESHOLD_L,   //PS Low Threshold L(Default :0x00)
    AP3216C_PS_LOW_THRESHOLD_H,   //PS Low Threshold H(Default :0x00)
    AP3216C_PS_HIGH_THRESHOLD_L,  //PS high Threshold L(Default :0xff)
    AP3216C_PS_HIGH_THRESHOLD_H,  //PS high Threshold H(Default :0xff)
};
typedef enum ap3216c_cmd ap3216c_cmd_t;


// --- Zephyr 风格的驱动 API ---

/**
 * @brief 执行 AP3216C 传感器软件复位。
 */
int ap3216c_reset_sensor(const struct i2c_dt_spec *i2c_spec);

/**
 * @brief 设置 AP3216C 工作模式。
 */
int ap3216c_set_mode(const struct i2c_dt_spec *i2c_spec, uint8_t mode);

/**
 * @brief 读取 AP3216C 的 ALS（环境光）原始数据。
 */
int ap3216c_read_als_raw(const struct i2c_dt_spec *i2c_spec, uint16_t *als_data);

/**
 * @brief 读取 AP3216C 的 PS（接近）原始数据。
 */
int ap3216c_read_ps_raw(const struct i2c_dt_spec *i2c_spec, uint16_t *ps_data);

/**
 * @brief 设置 AP3216C 传感器的单个参数或阈值。
 */
int ap3216c_set_param(const struct i2c_dt_spec *i2c_spec, ap3216c_cmd_t cmd, uint8_t value);

/**
 * @brief 获取 AP3216C 传感器的单个参数或阈值。
 */
int ap3216c_get_param(const struct i2c_dt_spec *i2c_spec, ap3216c_cmd_t cmd, uint8_t *value);


#endif // AP3216C_DRIVER_H
