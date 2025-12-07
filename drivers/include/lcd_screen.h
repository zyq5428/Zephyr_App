/**
 * @file lcd_screen.h
 * @brief LCD Screen Driver Wrapper for Pandora STM32L475
 */

#ifndef LCD_SCREEN_H
#define LCD_SCREEN_H

#include <zephyr/kernel.h>
#include <stdint.h>

/**
 * @brief Initialize the LCD screen.
 * 
 * @return 0 on success, negative errno on failure.
 */
int lcd_init(void);

/**
 * @brief Fill the entire screen with a specific color.
 * 
 * @param color 16-bit color value (RGB565).
 */
void lcd_fill_color(uint16_t color);

/**
 * @brief Draw a simple text string on the screen.
 *        Note: This is a simple validation function.
 * 
 * @param text Null-terminated string to draw.
 * @param x X coordinate.
 * @param y Y coordinate.
 * @param color Text color.
 */
void lcd_draw_text(const char *text, uint16_t x, uint16_t y, uint16_t color);

#endif /* LCD_SCREEN_H */
