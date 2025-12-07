/**
 * @file display_thread.c
 * @brief Display Thread Implementation
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "display_thread.h"
#include "lcd_screen.h"

LOG_MODULE_REGISTER(display_thread, CONFIG_LOG_DEFAULT_LEVEL);

/* Define stack size and priority */
#define DISPLAY_THREAD_STACK_SIZE 2048
#define DISPLAY_THREAD_PRIORITY 7

/* Define colors (RGB565) */
#define COLOR_RED   0xF800
#define COLOR_GREEN 0x07E0
#define COLOR_BLUE  0x001F
#define COLOR_BLACK 0x0000
#define COLOR_WHITE 0xFFFF

K_THREAD_DEFINE(display_tid, DISPLAY_THREAD_STACK_SIZE,
                display_thread_entry, NULL, NULL, NULL,
                DISPLAY_THREAD_PRIORITY, 0, 0);

void display_thread_entry(void *p1, void *p2, void *p3)
{
    LOG_INF("Display Thread Started");

    int ret = lcd_init();
    if (ret != 0) {
        LOG_ERR("Failed to initialize LCD: %d", ret);
        return;
    }

    lcd_fill_color(COLOR_BLACK);
    k_sleep(K_MSEC(500));

    lcd_draw_text("Hello Zephyr!", 10, 10, COLOR_WHITE);
    k_sleep(K_MSEC(1000));

    while (1) {
        LOG_INF("Displaying RED");
        lcd_fill_color(COLOR_RED);
        k_sleep(K_MSEC(1000));

        LOG_INF("Displaying GREEN");
        lcd_fill_color(COLOR_GREEN);
        k_sleep(K_MSEC(1000));

        LOG_INF("Displaying BLUE");
        lcd_fill_color(COLOR_BLUE);
        k_sleep(K_MSEC(1000));
    }
}
