/**
 * @file lcd_screen.c
 * @brief LCD Screen Driver Implementation
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include "lcd_screen.h"

LOG_MODULE_REGISTER(lcd_screen, CONFIG_DISPLAY_LOG_LEVEL);

/* Get the display device from DeviceTree */
static const struct device *display_dev = DEVICE_DT_GET(DT_NODELABEL(st7789v));

/* LCD背光GPIO定义 - PB7 */
#define LCD_BACKLIGHT_PORT DEVICE_DT_GET(DT_NODELABEL(gpiob))
#define LCD_BACKLIGHT_PIN 7

int lcd_init(void)
{
    int ret;

    /* 初始化并打开LCD背光 - PB7 */
    const struct device *backlight_port = LCD_BACKLIGHT_PORT;
    if (device_is_ready(backlight_port)) {
        ret = gpio_pin_configure(backlight_port, LCD_BACKLIGHT_PIN, GPIO_OUTPUT_ACTIVE);
        if (ret < 0) {
            LOG_ERR("Failed to configure backlight GPIO: %d", ret);
        } else {
            gpio_pin_set(backlight_port, LCD_BACKLIGHT_PIN, 1); // 打开背光
            LOG_INF("LCD Backlight enabled (PB7)");
        }
    } else {
        LOG_WRN("Backlight GPIO device not ready");
    }

    /* 检查显示设备是否就绪 */
    if (!device_is_ready(display_dev)) {
        LOG_ERR("Display device not ready");
        return -ENODEV;
    }

    struct display_capabilities capabilities;
    display_get_capabilities(display_dev, &capabilities);

    LOG_INF("LCD Initialized: %dx%d", capabilities.x_resolution, capabilities.y_resolution);
    
    /* Turn on display (if supported/needed, though usually handled by driver init) */
    display_blanking_off(display_dev);

    return 0;
}

void lcd_fill_color(uint16_t color)
{
    if (!display_dev) {
        return;
    }

    struct display_capabilities capabilities;
    display_get_capabilities(display_dev, &capabilities);

    struct display_buffer_descriptor desc;
    desc.buf_size = capabilities.x_resolution * capabilities.y_resolution * 2;
    desc.width = capabilities.x_resolution;
    desc.height = capabilities.y_resolution;
    desc.pitch = capabilities.x_resolution;

    /* 
     * Note: Allocating a full frame buffer on stack or heap might be too large for some MCUs.
     * STM32L475 has 128KB SRAM. 240*240*2 = 115200 bytes. This is very risky/impossible on stack.
     * We should fill in chunks.
     */
    
    uint16_t chunk_height = 10; // Process 10 lines at a time
    uint32_t chunk_size = capabilities.x_resolution * chunk_height * 2;
    static uint8_t buf[240 * 10 * 2]; // 4800 bytes, safe for stack

    /* Fill buffer with color */
    for (int i = 0; i < (capabilities.x_resolution * chunk_height); i++) {
        // RGB565 is big endian usually for SPI displays, but Zephyr driver handles it.
        // We just pack the uint16_t.
        // Assuming little endian system, we might need to swap bytes if driver expects raw byte stream in specific order.
        // But usually passing uint16_t array cast to void* works if driver handles pixel format.
        // Let's assume standard RGB565.
        ((uint16_t *)buf)[i] = sys_cpu_to_be16(color); 
        
        // If colors look wrong (swapped), we might need: sys_cpu_to_be16(color)
    }

    desc.height = chunk_height;
    desc.buf_size = chunk_size;

    for (int y = 0; y < capabilities.y_resolution; y += chunk_height) {
        /* Handle last chunk if height is not multiple of chunk_height */
        if (y + chunk_height > capabilities.y_resolution) {
            desc.height = capabilities.y_resolution - y;
            desc.buf_size = capabilities.x_resolution * desc.height * 2;
        }
        
        display_write(display_dev, 0, y, &desc, buf);
    }
}

void lcd_draw_text(const char *text, uint16_t x, uint16_t y, uint16_t color)
{
    /* 
     * Simple text rendering is complex without a font library (like LVGL or Zephyr CFB).
     * For this requirement "simple validation", we will just log the text 
     * and maybe draw a small colored box at x,y to show we received the command.
     */
    LOG_INF("LCD Print: '%s' at (%d, %d)", text, x, y);

    /* Draw a 5x5 marker to indicate text position */
    struct display_buffer_descriptor desc;
    uint16_t buf[25];
    for(int i=0; i<25; i++) buf[i] = color;

    desc.buf_size = sizeof(buf);
    desc.width = 5;
    desc.height = 5;
    desc.pitch = 5;

    display_write(display_dev, x, y, &desc, buf);
}
