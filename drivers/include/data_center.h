#ifndef DATA_CENTER_H
#define DATA_CENTER_H

#include <zephyr/kernel.h>

/* 引入传感器头文件 */
#include "aht10.h"
#include "ap3216c.h"
#include "icm20608.h"

/* 定义全局数据结构 */
typedef struct {
    struct k_mutex lock;      // 互斥锁，保证多线程读写安全
    
    // 数据区
    aht10_data_t env;        // 温湿度
    uint16_t lux;            // 光照
    icm20608_data_t imu_accel_gyro;       // 加速度和陀螺仪数据
    
    uint32_t last_update;    // 最后一次更新的时间戳
} system_data_t;

/* 声明全局变量，让其他 .c 文件都能看到它 */
extern system_data_t g_sys_data;

/* 提供线程安全的读写接口 */
void data_center_init(void);
void data_center_update_env(aht10_data_t *data);
void data_center_update_lux(uint16_t lux);
void data_center_update_imu(icm20608_data_t *data);
void data_center_get_snapshot(system_data_t *dest);


#endif