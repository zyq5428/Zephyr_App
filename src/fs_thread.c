#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/logging/log.h>
#include <string.h>

// 注册日志模块
LOG_MODULE_REGISTER(FS_CHECK, LOG_LEVEL_INF);

/* ---------------- 1. 文件系统配置 ---------------- */
// 获取设备树中的 filesystem 分区 ID
#define STORAGE_PARTITION_ID FIXED_PARTITION_ID(filesystem_partition)

// 定义 LittleFS 的配置结构
FS_LITTLEFS_DECLARE_DEFAULT_CONFIG(lfs_data);

static struct fs_mount_t fs_mnt = {
    .type = FS_LITTLEFS,
    .fs_data = &lfs_data,
    .storage_dev = (void *)STORAGE_PARTITION_ID,
    .mnt_point = "/lfs",
};

/**
 * @brief 文件系统自检函数
 * 执行挂载和一次性读写测试
 */
void fs_check_thread_entry(void *p1, void *p2, void *p3)
{
    int ret;
    struct fs_file_t file;
    const char *test_file_path = "/lfs/boot_test.txt";
    const char *test_string = "FS Self-Test Passed";
    char read_buf[32] = {0};

    LOG_INF("=== 开始文件系统硬件自检 ===");

    /* 1. 尝试挂载 */
    ret = fs_mount(&fs_mnt);
    if (ret < 0) {
        // 如果是 -84 (Corrupted)，Zephyr 的 littlefs 插件通常会自动格式化
        // 这里的错误处理主要针对硬件无法连接等严重问题
        LOG_ERR("文件系统挂载失败: %d", ret);
        return; 
    }
    LOG_INF("1. 挂载成功: %s", fs_mnt.mnt_point);

    /* 2. 基础读写测试 */
    fs_file_t_init(&file);

    // 写入测试文件
    ret = fs_open(&file, test_file_path, FS_O_CREATE | FS_O_RDWR);
    if (ret < 0) {
        LOG_ERR("2. 无法创建测试文件: %d", ret);
    } else {
        fs_write(&file, test_string, strlen(test_string));
        fs_close(&file);
        LOG_INF("2. 写入测试数据成功");

        // 重新读取并校验
        ret = fs_open(&file, test_file_path, FS_O_READ);
        if (ret == 0) {
            fs_read(&file, read_buf, sizeof(read_buf) - 1);
            fs_close(&file);

            if (strcmp(read_buf, test_string) == 0) {
                LOG_INF("3. 数据校验一致: [%s]", read_buf);
                LOG_INF("=== 文件系统自检通过！系统环境正常 ===");
            } else {
                LOG_ERR("3. 数据校验失败！读取内容: %s", read_buf);
            }
        }
    }

    /* 3. 线程退出 */
    LOG_INF("自检线程任务完成，正在退出并释放资源...");
    
    // 在 Zephyr 中，线程函数 return 后，该线程就会进入终止状态
    return; 
}

/* ---------------- 2. 线程定义 ---------------- */
// 设置较高的优先级 (例如 5)，确保它在存储和显示线程之前完成自检
#define FS_CHECK_PRIORITY 5
#define FS_CHECK_STACK_SIZE 2048

K_THREAD_DEFINE(fs_check_tid, FS_CHECK_STACK_SIZE,
                fs_check_thread_entry, NULL, NULL, NULL,
                FS_CHECK_PRIORITY, 0, 0);