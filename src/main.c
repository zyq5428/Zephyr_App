#include <zephyr/kernel.h>
#include "led_thread.h"
#include "ap3216c_thread.h"

int main(void)
{
    printk("Application starting up...\n");

    // 启动所有的独立线程
    start_led_thread();
    start_ap3216c_thread();

    // main 线程可以做一些不频繁的后台任务，或者直接退出（如果有其他线程在运行）
    return 0;
}