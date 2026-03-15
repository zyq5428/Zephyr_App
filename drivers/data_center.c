#include <zephyr/init.h>
#include <string.h>
#include "data_center.h"

// 实例化全局变量
/* ：
 * 1. 确保 4 字节对齐（ARM 访问速度最快）
 * 2. 初始化为 {0}，防止结构体里出现随机的“垃圾数据”
 */
// __attribute__((section("SRAM1"))) 
// __attribute__((aligned(4)))
system_data_t g_sys_data = {0};

void data_center_init(void) {
    k_mutex_init(&g_sys_data.lock);
    // 注意：手动分配到特殊段的变量，有时不会被系统自动清零，
    // 所以初始化时最好显式清空。
    k_mutex_lock(&g_sys_data.lock, K_FOREVER);
    memset(&g_sys_data, 0, sizeof(system_data_t));
    k_mutex_unlock(&g_sys_data.lock);
}

// 传感器调用：更新温湿度
void data_center_update_env(aht10_data_t *data) {
    k_mutex_lock(&g_sys_data.lock, K_FOREVER);
    g_sys_data.env = *data;
    g_sys_data.last_update = k_uptime_get_32();
    k_mutex_unlock(&g_sys_data.lock);
}

// 传感器调用：更新光照
void data_center_update_lux(uint16_t lux) {
    k_mutex_lock(&g_sys_data.lock, K_FOREVER);
    g_sys_data.lux = lux;
    g_sys_data.last_update = k_uptime_get_32();
    k_mutex_unlock(&g_sys_data.lock);
}

// 传感器调用：更新IMU数据
void data_center_update_imu(icm20608_data_t *data) {  
    k_mutex_lock(&g_sys_data.lock, K_FOREVER);
    g_sys_data.imu_accel_gyro = *data;
    g_sys_data.last_update = k_uptime_get_32();
    k_mutex_unlock(&g_sys_data.lock);
}

// 业务线程调用：获取一份完整的数据快照
void data_center_get_snapshot(system_data_t *dest) {
    k_mutex_lock(&g_sys_data.lock, K_FOREVER);
    // 拷贝整块数据到目标地址
    memcpy(dest, &g_sys_data, sizeof(system_data_t));
    k_mutex_unlock(&g_sys_data.lock);
}

static int auto_init_data_center(void)
{
    data_center_init();
    return 0;
}

/* * APPLICATION 级别初始化：
 * 这会让系统在进入 main 之前，自动调用这个函数
 * 优先级设为 50，确保它在硬件驱动之后、应用线程之前运行
 */
SYS_INIT(auto_init_data_center, APPLICATION, 50);