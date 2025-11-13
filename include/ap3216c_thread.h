/*
 * application/include/ap3216c_thread.h
 * AP3216C 传感器数据读取线程的头文件
 */

#ifndef AP3216C_THREAD_H
#define AP3216C_THREAD_H

#include <zephyr/types.h>
#include "ap3216c.h"

/**
 * @brief 启动 AP3216C 传感器读取线程
 *
 * 该函数负责初始化并启动 AP3216C 传感器读取线程。
 * 线程会初始化 I2C 设备，然后周期性地读取 ALS/PS 数据。
 */
void start_ap3216c_thread(void);

#endif // AP3216C_THREAD_H