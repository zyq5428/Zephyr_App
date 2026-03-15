#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/logging/log.h>
#include <string.h> // 引入 memcmp 用于数据对比

// 启用日志记录，模块名称为 NOR_TASK
LOG_MODULE_REGISTER(NOR_TASK, LOG_LEVEL_INF);

/* ---------------- 配置参数 ---------------- */
// 获取设备树中定义的 w25q128jv 节点
#define FLASH_NODE DT_NODELABEL(w25q128jv)

// 根据你提供的分区表截图，我们选择“用户使用区”进行测试
// 偏移地址：0x00803000
#define TEST_OFFSET           0x00803000
// 测试数据大小 (W25Q128 最小擦除单位是 4096 字节的一个扇区)
#define TEST_SECTOR_SIZE      4096
// XIP 内存映射基地址 (STM32L4 QSPI 固定为 0x90000000)
#define QSPI_MAPPED_BASE      0x90000000

/* 获取 Flash 设备实例 */
static const struct device *flash_dev = DEVICE_DT_GET(FLASH_NODE);

/**
 * @brief Nor Flash 测试线程入口函数
 */
void norflash_thread_entry(void *p1, void *p2, void *p3)
{
    int ret;
    uint8_t write_data[] = "Hello Pandora! W25Q128 Test Success.";
    uint8_t read_data[sizeof(write_data)];
    
    LOG_INF("Nor Flash 测试线程已启动");

    /* 1. 检查 Flash 设备是否就绪 */
    if (!device_is_ready(flash_dev)) {
        LOG_ERR("错误：Flash 设备未就绪！请确认 prj.conf 中开启了 QSPI 驱动。");
        return;
    }
    LOG_INF("找到设备: %s", flash_dev->name);

    while (1) {
        LOG_INF("========== 开始新一轮 Nor Flash 测试 ==========");

        /* 2. 擦除操作 (Flash 写入前必须擦除) */
        // 注意：擦除起始地址和大小都必须按照扇区(4KB)对齐
        LOG_INF("1. 正在擦除偏移地址 0x%x 处的扇区...", TEST_OFFSET);
        ret = flash_erase(flash_dev, TEST_OFFSET, TEST_SECTOR_SIZE);
        if (ret != 0) {
            LOG_ERR("擦除失败! 错误码: %d", ret);
        } else {
            LOG_INF("擦除成功！");

            /* 3. 写入操作 (使用标准 API) */
            LOG_INF("2. 正在写入数据: \"%s\"", write_data);
            ret = flash_write(flash_dev, TEST_OFFSET, write_data, sizeof(write_data));
            if (ret != 0) {
                LOG_ERR("写入失败! 错误码: %d", ret);
            } else {
                LOG_INF("写入成功！");

                /* 4. 读取操作 (使用标准 API) */
                LOG_INF("3. 正在通过 API 读取数据进行校验...");
                ret = flash_read(flash_dev, TEST_OFFSET, read_data, sizeof(read_data));
                if (ret == 0) {
                    LOG_INF("API 读取到的内容: \"%s\"", read_data);
                } else {
                    LOG_ERR("读取失败! 错误码: %d", ret);
                }
            }
        }

        /* 5. XIP 指针读取验证 (直接读取映射地址) */
        // 计算映射后的绝对内存地址
        uint32_t xip_addr = QSPI_MAPPED_BASE + TEST_OFFSET;
        // 定义一个只读指针指向该地址
        const char *xip_ptr = (const char *)xip_addr;
        
        LOG_INF("4. 正在通过 XIP 指针直接读取 (地址: 0x%08x)...", xip_addr);
        // 只有在 menuconfig 中开启了 MemoryMapped 模式，此步才有效
        LOG_INF("指针读取的内容: \"%s\"", xip_ptr);

        /* 【核心新增】：判断指针数据和写入的数据是否完全一致 */
        if (memcmp(xip_ptr, write_data, sizeof(write_data)) == 0) {
            LOG_INF("成功：指针读取数据与写入一致！内容: \"%s\"", xip_ptr);
            LOG_INF(">>> 验证通过，线程将进入永久休眠以保护 Flash 寿命 <<<");
            
            /* 5. 永久休眠 */
            // 方式 A: 让当前线程进入永久睡眠状态
            k_sleep(K_FOREVER); 
        } else {
            LOG_WRN("警告：校验不一致，10秒后尝试重新测试...");
            k_msleep(10000);
        }
    }
}

/* ---------------- 线程定义 ---------------- */
#define NOR_STACK_SIZE 2048
#define NOR_PRIORITY 17

K_THREAD_DEFINE(nor_tid, NOR_STACK_SIZE, 
                norflash_thread_entry, NULL, NULL, NULL,
                NOR_PRIORITY, 0, 0);