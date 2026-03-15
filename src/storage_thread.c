#include <zephyr/kernel.h>
#include <zephyr/fs/fs.h>
#include <zephyr/logging/log.h>
#include <stdio.h>
#include <string.h>

#include "data_center.h"

LOG_MODULE_REGISTER(STORAGE_TASK, LOG_LEVEL_INF);

#define SAVE_INTERVAL_MS  (5 * 60 * 1000) // 正式使用设为 5 分钟
#define CSV_FILE_PATH     "/lfs/data.csv"

void storage_thread_entry(void *p1, void *p2, void *p3)
{
    int ret;
    struct fs_file_t file;
    fs_file_t_init(&file);

    LOG_INF("数据存储线程已就绪 (CSV 格式)");

    while (1) {
        /* 1. 周期性等待 */
        k_msleep(SAVE_INTERVAL_MS);

        system_data_t snap;
        data_center_get_snapshot(&snap);

        /* 构造 CSV 数据行 */
        char row[128];
        // 确保 prj.conf 中有 CONFIG_CBPRINTF_FP_SUPPORT=y
        snprintf(row, sizeof(row), "%u,%.2f,%.2f,%u\n", 
                 (uint32_t)(k_uptime_get() / 1000), 
                 (double)snap.env.temperature, 
                 (double)snap.env.humidity, 
                 snap.lux);

        /* 4. 执行文件写入 */
        ret = fs_open(&file, CSV_FILE_PATH, FS_O_CREATE | FS_O_WRITE | FS_O_APPEND);
        if (ret == 0) {
            fs_write(&file, row, strlen(row));
            fs_close(&file);
            LOG_DBG("[存储成功] 内容: %s", row);
        } else {
            LOG_ERR("文件打开失败: %d", ret);
        }
    }
}

/* ---------------- 线程定义 ---------------- */
/* 定义存储线程：优先级 12 (低于显示和传感器) */
#define STORAGE_PRIORITY 12
#define STORAGE_STACK_SIZE 2048

K_THREAD_DEFINE(storage_tid, STORAGE_STACK_SIZE, 
                storage_thread_entry, NULL, NULL, NULL, 
                STORAGE_PRIORITY, 0, 0);