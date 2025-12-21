#include <zephyr/kernel.h>

int main(void)
{
    // 仅仅打印一行启动信息即可
    printk("System is up and running...\n");

    // 无需调用 start_xxx_thread()，因为它们已经自动运行了
    
    // 关键：进入永久休眠，把所有的 CPU 资源和栈空间释放给那些重要线程
    while (1) {
        k_sleep(K_FOREVER);
    }
    return 0;
}
