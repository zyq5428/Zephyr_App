/*
 * icm20608_thread.c
 * 应用层线程：支持轮询和中断模式的灵活切换
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include "icm20608.h"

LOG_MODULE_REGISTER(ICM_TASK, LOG_LEVEL_INF);

/* 获取设备树节点标识符 */
#define ICM_NODE DT_NODELABEL(icm20608)

/* 1. 获取 I2C 规格 */
static const struct i2c_dt_spec dev_i2c = I2C_DT_SPEC_GET(ICM_NODE);

/* --- 中断相关组件 --- */
/* 使用条件宏判断设备树中是否存在中断引脚定义，增加代码健壮性 */
#if DT_NODE_HAS_PROP(ICM_NODE, int_gpios)
static const struct gpio_dt_spec dev_int = GPIO_DT_SPEC_GET(ICM_NODE, int_gpios);
static struct gpio_callback icm_gpio_cb;


/* 信号量：中断通知线程读取 */
static K_SEM_DEFINE(icm_sem, 0, 1); 
/* 定义消息队列 */
/* 参数：队列名, 数据大小, 最大消息数(1代表只保留最新), 对齐字节 */
K_MSGQ_DEFINE(imu_msgq, sizeof(icm20608_data_t), 1, 4);

/* 中断处理函数 */
void icm_isr_handler(const struct device *port, struct gpio_callback *cb, uint32_t pins) {
    k_sem_give(&icm_sem);
}
#endif

void icm20608_thread_entry(void *p1, void *p2, void *p3)
{
    int ret;
    icm20608_data_t sensor_data;

    LOG_INF("ICM20608 Thread starting...");

    /* ========================================================== */
    /* 模式选择：请根据实际情况注释掉不需要的一种 */
    
    // 【模式 A：主动轮询模式】—— 不依赖 PD0 引脚，只要 I2C 通就能打印
    // ret = icm20608_init_polling(&dev_i2c);

    // 【模式 B：中断触发模式】—— 依赖 PD0 引脚，只有传感器数据准备好才打印
    #if DT_NODE_HAS_PROP(ICM_NODE, int_gpios)
    ret = icm20608_init_interrupt(&dev_i2c, &dev_int, &icm_gpio_cb, icm_isr_handler);
    #else
    LOG_ERR("DeviceTree overlay lacks 'int-gpios'. Cannot use Interrupt mode.");
    return;
    #endif
    /* ========================================================== */

    if (ret != 0) {
        LOG_ERR("ICM20608 Init failed! Error code: %d", ret);
        return;
    }

    while (1) {
        /* --- 如果是中断模式，则在此等待信号量 --- */
        #if DT_NODE_HAS_PROP(ICM_NODE, int_gpios)
        k_sem_take(&icm_sem, K_FOREVER);
        #endif 
        
        // 读取并打印
        ret = icm20608_read_data(&dev_i2c, &sensor_data);
        if (ret == 0) {
            // LOG_DBG("ACC: X=%.2f Y=%.2f Z=%.2f | GYRO: X=%.2f Y=%.2f Z=%.2f | Temp: %.2f",
            //             (double)sensor_data.accel_x, (double)sensor_data.accel_y, (double)sensor_data.accel_z,
            //             (double)sensor_data.gyro_x, (double)sensor_data.gyro_y, (double)sensor_data.gyro_z,
            //             (double)sensor_data.temp);

            // 将数据放入消息队列供其他模块使用
            k_msgq_put(&imu_msgq, &sensor_data, K_NO_WAIT);
        }

        // 轮询模式下必须有延时，防止串口刷屏；中断模式下该延时可极大减小或注释掉
        k_msleep(20);
    }
}

/* --- 线程定义和启动 --- */

// 定义栈空间、优先级和线程入口函数
#define ICM20608_STACK_SIZE 2048
#define ICM20608_PRIORITY 7 // 较高的优先级，因为是中断驱动

K_THREAD_DEFINE(icm_tid, ICM20608_STACK_SIZE, 
                icm20608_thread_entry, NULL, NULL, NULL,
                ICM20608_PRIORITY, 0, 0);
