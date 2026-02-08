/*
 * peripherals/aht10_drv.c
 * AHT10 传感器驱动实现 (Zephyr 风格)
 */

#include <zephyr/logging/log.h>
#include <errno.h>

#include "aht10.h"

// 注册日志模块，标签为 AHT10_DRV
LOG_MODULE_REGISTER(AHT10_DRV, LOG_LEVEL_INF);

/* --- 辅助函数 --- */

/**
 * @brief 向 AHT10 发送命令和参数
 * AHT10 的写操作通常包含：命令字节 + 参数0 + 参数1
 */
static int aht10_write_cmd(const struct i2c_dt_spec *i2c_spec, uint8_t cmd, uint8_t *args, uint8_t arg_len)
{
    uint8_t buf[4]; // 最大长度：1个命令 + 2个参数
    
    if (!device_is_ready(i2c_spec->bus)) {
        LOG_ERR("I2C bus (%s) not ready.", i2c_spec->bus->name);
        return -ENODEV;
    }

    buf[0] = cmd;
    // 将参数拷贝到发送缓冲区
    if (arg_len > 0 && args != NULL) {
        for (int i = 0; i < arg_len; i++) {
            buf[i + 1] = args[i];
        }
    }

    // 发送数据 (命令 + 参数)
    return i2c_write_dt(i2c_spec, buf, arg_len + 1);
}

/* --- 驱动 API 实现 --- */

int aht10_soft_reset(const struct i2c_dt_spec *i2c_spec)
{
    // 软复位命令不需要参数
    return aht10_write_cmd(i2c_spec, AHT10_CMD_SOFT_RESET, NULL, 0);
}

int aht10_init_sensor(const struct i2c_dt_spec *i2c_spec)
{
    int ret;
    uint8_t init_args[2] = {0x08, 0x00}; // 校准参数，参考数据手册或原厂代码

    // 1. 发送初始化/校准命令 0xE1 0x08 0x00
    ret = aht10_write_cmd(i2c_spec, AHT10_CMD_INIT, init_args, 2);
    if (ret != 0) {
        LOG_ERR("Failed to send init cmd: %d", ret);
        return ret;
    }

    // 2. 等待校准完成，手册建议延时几百毫秒
    k_msleep(400);

    LOG_INF("AHT10 initialized.");
    return 0;
}

int aht10_read_data(const struct i2c_dt_spec *i2c_spec, aht10_data_t *data)
{
    int ret;
    uint8_t trigger_args[2] = {0x33, 0x00}; // 触发测量参数
    uint8_t buf[6]; // 用于存储读取回来的6字节数据
    uint32_t raw_humi, raw_temp;

    // 1. 发送触发测量命令: 0xAC 0x33 0x00
    ret = aht10_write_cmd(i2c_spec, AHT10_CMD_TRIGGER, trigger_args, 2);
    if (ret != 0) {
        return ret;
    }

    // 2. 等待测量完成
    // 数据手册说明测量通常需要 75ms 以上，这里给 80ms 以保安全
    k_msleep(80);

    // 3. 读取 6 个字节的数据
    // AHT10 不需要像 AP3216C 那样先写寄存器地址再读，而是直接读取 I2C 数据流
    // Byte 0: 状态字
    // Byte 1: 湿度 [19:12]
    // Byte 2: 湿度 [11:4]
    // Byte 3: 湿度 [3:0] (高4位) | 温度 [19:16] (低4位)
    // Byte 4: 温度 [15:8]
    // Byte 5: 温度 [7:0]
    ret = i2c_read_dt(i2c_spec, buf, 6);
    if (ret != 0) {
        LOG_ERR("Failed to read data bytes: %d", ret);
        return ret;
    }

    // 4. 检查状态字 (Byte 0)
    // Bit 7 为 1 表示设备忙，Bit 3 为 0 表示未校准
    if ((buf[0] & AHT10_STATUS_BUSY) != 0) {
        LOG_WRN("Sensor is busy (Bit7 is 1)");
        // 实际应用中可能需要重试，这里暂时继续解析
    }
    if ((buf[0] & AHT10_STATUS_CALIBRATED) == 0) {
        LOG_WRN("Sensor not calibrated (Bit3 is 0), consider re-init");
        // 如果发现未校准，可以尝试再次调用 aht10_init_sensor
    }

    // 5. 解析湿度数据 (20位)
    // 拼接: Byte1 << 12 | Byte2 << 4 | Byte3 >> 4
    raw_humi = ((uint32_t)buf[1] << 12) | ((uint32_t)buf[2] << 4) | ((uint32_t)buf[3] >> 4);

    // 6. 解析温度数据 (20位)
    // 拼接: (Byte3 & 0x0F) << 16 | Byte4 << 8 | Byte5
    raw_temp = (((uint32_t)buf[3] & 0x0F) << 16) | ((uint32_t)buf[4] << 8) | (uint32_t)buf[5];

    // 7. 转换为物理量 (标准公式)
    // 湿度 %RH = (raw / 2^20) * 100
    data->humidity = (float)raw_humi * 100.0f / 1048576.0f;

    // 温度 °C = ((raw / 2^20) * 200) - 50
    data->temperature = ((float)raw_temp * 200.0f / 1048576.0f) - 50.0f;

    return 0;
}