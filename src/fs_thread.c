#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(FS_TASK, LOG_LEVEL_INF);

/* ---------------- 1. 配置 LittleFS ---------------- */

// 这里的 "filesystem" 必须对应你设备树里 partition@900000 的 label 名
#define STORAGE_PARTITION_ID FIXED_PARTITION_ID(filesystem_partition)

// 声明 LittleFS 的私有数据结构
FS_LITTLEFS_DECLARE_DEFAULT_CONFIG(lfs_data);

static struct fs_mount_t mount_point = {
    .type = FS_LITTLEFS,
    .fs_data = &lfs_data,
    .storage_dev = (void *)STORAGE_PARTITION_ID, // 绑定分区 ID
    .mnt_point = "/lfs",                         // 挂载点（根目录名称）
};

void fs_thread_entry(void *p1, void *p2, void *p3)
{
    int ret;
    struct fs_file_t file;
    char read_buf[64];
    const char *test_msg = "Pandora Log: Temp=25.5C, Time=12:00";

    fs_file_t_init(&file);

    LOG_INF("文件系统线程启动...");

    /* ---------------- 2. 挂载文件系统 ---------------- */
    // 如果 Flash 是空的，挂载可能会失败，这时需要格式化
    ret = fs_mount(&mount_point);
    if (ret < 0) {
        LOG_WRN("挂载失败 (%d)，尝试格式化 Flash...", ret);
        // 这里可以添加格式化逻辑，通常第一次使用需要格式化
    }
    LOG_INF("LittleFS 挂载成功！路径: %s", mount_point.mnt_point);

    while (1) {
        LOG_INF("---------- 开始文件读写测试 ----------");

        /* ---------------- 3. 创建并写入文件 ---------------- */
        // FS_O_CREATE: 不存在则创建; FS_O_RDWR: 读写模式; FS_O_APPEND: 追加模式
        ret = fs_open(&file, "/lfs/log.txt", FS_O_CREATE | FS_O_RDWR | FS_O_APPEND);
        if (ret < 0) {
            LOG_ERR("打开文件失败: %d", ret);
        } else {
            // 写入一行数据
            fs_write(&file, test_msg, strlen(test_msg));
            fs_write(&file, "\n", 1);
            LOG_INF("成功写入一行日志到 /lfs/log.txt");
            fs_close(&file);
        }

        /* ---------------- 4. 读取文件内容 ---------------- */
        ret = fs_open(&file, "/lfs/log.txt", FS_O_READ);
        if (ret >= 0) {
            memset(read_buf, 0, sizeof(read_buf));
            fs_read(&file, read_buf, sizeof(read_buf) - 1);
            LOG_INF("文件当前内容: \n%s", read_buf);
            fs_close(&file);
        }

        LOG_INF("---------- 测试完成，等待 1 分钟 ----------");
        k_msleep(60000); // 每分钟记录一次，保护 Flash
    }
}

/* ---------------- 线程定义 ---------------- */
#define FS_STACK_SIZE 4096
#define FS_PRIORITY 11

K_THREAD_DEFINE(fs_tid, FS_STACK_SIZE, 
                fs_thread_entry, NULL, NULL, NULL, 
                FS_PRIORITY, 0, 0);