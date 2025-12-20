/*
 * Copyright (c) 2018 PHYTEC Messtechnik GmbH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

// 包含 Zephyr 日志系统的头文件
#include <zephyr/logging/log.h>
// 注册当前驱动模块的日志名称和日志级别。
// LOG_MODULE_REGISTER(模块名, 日志级别); 模块名为 ssd1306，日志级别由 Kconfig 宏 CONFIG_DISPLAY_LOG_LEVEL 决定
LOG_MODULE_REGISTER(ssd1306, LOG_LEVEL_INF);

// 标准 C 库头文件，用于字符串和内存操作
#include <string.h>
// Zephyr 设备驱动框架头文件
#include <zephyr/device.h>
// Zephyr 初始化头文件，用于 POST_KERNEL 等初始化阶段宏
#include <zephyr/init.h>
// Zephyr Display 驱动 API 头文件，是 LVGL 与本驱动通信的接口定义
#include <zephyr/drivers/display.h>
// GPIO 驱动头文件，用于控制复位、供电等引脚
#include <zephyr/drivers/gpio.h>
// I2C 驱动头文件，用于 I2C 总线通信
#include <zephyr/drivers/i2c.h>
// SPI 驱动头文件，用于 SPI 总线通信
#include <zephyr/drivers/spi.h>
// Zephyr 内核头文件，用于 k_sleep 等内核函数
#include <zephyr/kernel.h>

// 包含自定义的 SSD1306 芯片命令宏定义文件
#include "ssd1306_regs.h"

#define CUSTOM_SSD1306 1 // 定义一个宏，表示使用自定义的 SSD1306 驱动
#define SSD1306_ADDRESSING_MODE 0x20 // 寻址模式模式模式0x20为水平寻址模式，0x10为页寻址模式，0x00为列寻址模式

// --- 硬件初始化常量定义 ---
#define SSD1306_CLOCK_DIV_RATIO		0x0  // 时钟分频比参数 (D5H 命令的低 4 位)
#if CUSTOM_SSD1306
#define SSD1306_CLOCK_FREQUENCY		0xF  // 振荡器频率参数 (D5H 命令的高 4 位)
#else
#define SSD1306_CLOCK_FREQUENCY		0x8  // 振荡器频率参数 (D5H 命令的高 4 位)
#endif
#define SSD1306_PANEL_VCOM_DESEL_LEVEL	0x20 // VCOMH 去激励电压等级参数 (DBH 命令的第二个字节)
#define SSD1306_PANEL_PUMP_VOLTAGE	SSD1306_SET_PUMP_VOLTAGE_90 // 内部电荷泵电压设置 (8DH 命令的第二个字节)

#define SSD1306_PANEL_VCOM_DESEL_LEVEL_SSD1309  0x34 // 兼容 SSD1309 芯片的 VCOMH 设置

// 如果 Kconfig 中未定义 SSD1306_ADDRESSING_MODE，则默认为水平寻址模式
#ifndef SSD1306_ADDRESSING_MODE
#define SSD1306_ADDRESSING_MODE		(SSD1306_SET_MEM_ADDRESSING_HORIZONTAL)
#endif
// 定义上电或复位后的延迟时间 (毫秒)
// #define SSD1306_SUPPLY_DELAY            100

// --- 驱动程序数据结构定义 ---

// 联合体：用于存储 I2C 或 SPI 总线的设备句柄，实现总线类型统一处理
union ssd1306_bus {
	struct i2c_dt_spec i2c; // I2C 设备配置结构体
	struct spi_dt_spec spi; // SPI 设备配置结构体
};

// --- 函数指针类型定义 (用于封装 I2C/SPI 操作) ---

// 检查总线是否就绪的函数指针
typedef bool (*ssd1306_bus_ready_fn)(const struct device *dev);
// 写入总线数据的函数指针。command=true 表示发送命令，command=false 表示发送数据
typedef int (*ssd1306_write_bus_fn)(const struct device *dev, uint8_t *buf, size_t len,
				    bool command);
// 获取总线名称的函数指针
typedef const char *(*ssd1306_bus_name_fn)(const struct device *dev);

// 驱动的**配置**结构体 (config)：从设备树读取的只读硬件参数
struct ssd1306_config {
	union ssd1306_bus bus;       // 总线设备句柄 (I2C/SPI)
	struct gpio_dt_spec data_cmd; // 数据/命令 (D/C) 控制引脚 (仅用于 SPI)
	struct gpio_dt_spec reset;    // 复位 (RES) 引脚配置
	struct gpio_dt_spec supply;   // 供电 (VCC) 控制引脚配置
	ssd1306_bus_ready_fn bus_ready; // 检查总线就绪的函数实现
	ssd1306_write_bus_fn write_bus; // 写入总线数据的函数实现
	ssd1306_bus_name_fn bus_name;   // 获取总线名称的函数实现
	uint16_t height;               // 屏幕高度 (例如 64)
	uint16_t width;                // 屏幕宽度 (例如 128)
	uint8_t segment_offset;       // 段起始地址偏移 (用于适应不同厂商的屏幕)
	uint8_t page_offset;          // 页起始地址偏移
	uint8_t display_offset;       // 显示起始行偏移
	uint8_t multiplex_ratio;      // 多路复用比 (通常是 Height - 1，例如 63)
	uint8_t prechargep;           // 预充电周期
	bool segment_remap;           // 段重新映射标志 (控制 A0/A1 翻转)
	bool com_invdir;              // COM 扫描方向反转标志 (控制 C0/C8 翻转)
	bool com_sequential;          // COM 引脚硬件配置 (序列/交替)
	bool color_inversion;         // 颜色反转标志 (初始设置黑白反转)
	bool ssd1309_compatible;      // SSD1309 兼容标志
	bool sh1106_compatible;       // SH1106 兼容标志
	int ready_time_ms;            // 上电或复位后的等待时间
	bool use_internal_iref;       // 使用内部电流参考标志
};

// 驱动的**数据**结构体 (data)：运行时可读写的状态信息
struct ssd1306_data {
	enum display_pixel_format pf; // 当前使用的像素格式 (PIXEL_FORMAT_MONO10 或 PIXEL_FORMAT_MONO01)
};

// --- I2C 总线操作实现 ---
// 宏定义：检查设备树中是否有 SSD1306/SSD1309/SH1106 通过 I2C 总线连接且状态为 okay
#if (DT_HAS_COMPAT_ON_BUS_STATUS_OKAY(solomon_ssd1306fb, i2c) || \
	DT_HAS_COMPAT_ON_BUS_STATUS_OKAY(solomon_ssd1309fb, i2c) || \
	DT_HAS_COMPAT_ON_BUS_STATUS_OKAY(sinowealth_sh1106, i2c) || \
	DT_HAS_COMPAT_ON_BUS_STATUS_OKAY(custom_sh1106, i2c))
// 检查 I2C 总线是否就绪
static bool ssd1306_bus_ready_i2c(const struct device *dev)
{
	const struct ssd1306_config *config = dev->config;
	// 调用 Zephyr I2C API 检查 I2C 设备是否准备就绪
	return i2c_is_ready_dt(&config->bus.i2c);
}

// I2C 总线写入函数
static int ssd1306_write_bus_i2c(const struct device *dev, uint8_t *buf, size_t len, bool command)
{
	const struct ssd1306_config *config = dev->config;
	// 使用 i2c_burst_write_dt 批量写入数据
    // 第一个参数是 I2C 设备配置
    // 第二个参数是控制字节：command 为 true 则为命令 (0x00)，false 则为数据 (0x40)
    // 第三个参数是数据缓冲区
    // 第四个参数是数据长度
	return i2c_burst_write_dt(&config->bus.i2c,
				  command ? SSD1306_CONTROL_ALL_BYTES_CMD :
				  SSD1306_CONTROL_ALL_BYTES_DATA,
				  buf, len);
}
// 获取 I2C 总线名称
static const char *ssd1306_bus_name_i2c(const struct device *dev)
{
	const struct ssd1306_config *config = dev->config;
	return config->bus.i2c.bus->name;
}
#endif // I2C 宏结束

// --- SPI 总线操作实现 (被省略，但结构类似) ---
// (此处省略了 SPI 实现，但在完整的驱动文件中会包含，用于处理 SPI 接口的 OLED 屏)
// ...

// --- 通用总线操作内联函数 (用于简化代码) ---

// 检查总线就绪的通用内联函数
static inline bool ssd1306_bus_ready(const struct device *dev)
{
	const struct ssd1306_config *config = dev->config;
	return config->bus_ready(dev); // 调用 I2C 或 SPI 对应的实现函数
}
// 写入总线数据的通用内联函数
static inline int ssd1306_write_bus(const struct device *dev, uint8_t *buf, size_t len,
				    bool command)
{
	const struct ssd1306_config *config = dev->config;
	return config->write_bus(dev, buf, len, command); // 调用 I2C 或 SPI 对应的实现函数
}
// 获取总线名称的通用内联函数
static inline const char *ssd1306_bus_name(const struct device *dev)
{
	const struct ssd1306_config *config = dev->config;
	return config->bus_name(dev); // 调用 I2C 或 SPI 对应的实现函数
}


// --- 硬件初始化命令发送函数 ---

// 设置面板方向 (段重映射和 COM 扫描方向)
static inline int ssd1306_set_panel_orientation(const struct device *dev)
{
	const struct ssd1306_config *config = dev->config;
	uint8_t cmd_buf[] = {
        // A0/A1 翻转，由 segment_remap 决定
        (config->segment_remap ? SSD1306_SET_SEGMENT_MAP_REMAPED : SSD1306_SET_SEGMENT_MAP_NORMAL),
        // C0/C8 翻转，由 com_invdir 决定
        (config->com_invdir ? SSD1306_SET_COM_OUTPUT_SCAN_FLIPPED : SSD1306_SET_COM_OUTPUT_SCAN_NORMAL)
    };
    // 发送命令
	return ssd1306_write_bus(dev, cmd_buf, sizeof(cmd_buf), true);
}

// 设置时序 (时钟分频/频率, 预充电周期, VCOMH 电平)
static inline int ssd1306_set_timing_setting(const struct device *dev)
{
	const struct ssd1306_config *config = dev->config;
	uint8_t cmd_buf[] = {
        SSD1306_SET_CLOCK_DIV_RATIO, // D5H 命令
        // D5H 命令的第二个字节：高 4 位为频率，低 4 位为分频比
        (SSD1306_CLOCK_FREQUENCY << 4) | SSD1306_CLOCK_DIV_RATIO,
        SSD1306_SET_CHARGE_PERIOD,   // D9H 命令
        config->prechargep,          // 预充电周期参数
        SSD1306_SET_VCOM_DESELECT_LEVEL, // DBH 命令
        // 根据是否兼容 SSD1309 设置 VCOMH 电平参数
        config->ssd1309_compatible ? SSD1306_PANEL_VCOM_DESEL_LEVEL_SSD1309 : SSD1306_PANEL_VCOM_DESEL_LEVEL
    };
    // 发送命令
	return ssd1306_write_bus(dev, cmd_buf, sizeof(cmd_buf), true);
}

// 设置硬件配置 (起始行、显示偏移、硬件引脚配置、多路复用比)
static inline int ssd1306_set_hardware_config(const struct device *dev)
{
	const struct ssd1306_config *config = dev->config;
	uint8_t cmd_buf[] = {
		SSD1306_SET_START_LINE,       // 40H
		SSD1306_SET_DISPLAY_OFFSET,   // D3H
		config->display_offset,       // 显示偏移参数
		SSD1306_SET_PADS_HW_CONFIG,   // DAH 命令
		// 设置 COM 引脚硬件配置：序列或交替
		(config->com_sequential ? SSD1306_SET_PADS_HW_SEQUENTIAL : SSD1306_SET_PADS_HW_ALTERNATIVE),
		SSD1306_SET_MULTIPLEX_RATIO,  // A8H 命令
		config->multiplex_ratio,      // 多路复用比参数
	};
    // 发送命令
	return ssd1306_write_bus(dev, cmd_buf, sizeof(cmd_buf), true);
}

// 设置电荷泵 (用于内部供电)
static inline int ssd1306_set_charge_pump(const struct device *dev)
{
	const struct ssd1306_config *config = dev->config;
	uint8_t cmd_buf[] = {
        // 根据是否兼容 SH1106 选择电荷泵开启命令
		(config->sh1106_compatible ? SH1106_SET_DCDC_MODE : SSD1306_SET_CHARGE_PUMP_ON),
		// 开启电荷泵
		(config->sh1106_compatible ? SH1106_SET_DCDC_ENABLED : SSD1306_SET_CHARGE_PUMP_ON_ENABLED),
		SSD1306_PANEL_PUMP_VOLTAGE, // 设置电压参数
	};
    // 发送命令
	return ssd1306_write_bus(dev, cmd_buf, sizeof(cmd_buf), true);
}

// 设置电荷泵SH1106 (用于内部供电)
static inline int sh1106_set_charge_pump(const struct device *dev)
{
	uint8_t cmd_buf[] = {
        //选择电荷泵开启命令
		SSD1306_SET_CHARGE_PUMP_ON,
		// 开启电荷泵
		SSD1306_SET_CHARGE_PUMP_ON_ENABLED,
	};
    // 发送命令
	return ssd1306_write_bus(dev, cmd_buf, sizeof(cmd_buf), true);
}

// 设置电流参考模式
static inline int ssd1306_set_iref_mode(const struct device *dev)
{
	int ret = 0; // 返回值
	const struct ssd1306_config *config = dev->config;
	uint8_t cmd_buf[] = {
		SSD1306_SET_IREF_MODE,
		SSD1306_SET_IREF_MODE_INTERNAL, // 使用内部参考电流的参数
	};

	// 仅当设备树中启用 use-internal-iref 标志时才发送此命令
	if (config->use_internal_iref) {
		ret = ssd1306_write_bus(dev, cmd_buf, sizeof(cmd_buf), true);
	}
	return ret; // 返回操作结果
}

// --- Zephyr Display API 辅助函数 ---

// 恢复显示 (亮屏/取消消隐)
static int ssd1306_resume(const struct device *dev)
{
	const struct ssd1306_config *config = dev->config;
	uint8_t cmd_buf[] = {
		SSD1306_DISPLAY_ON, // AFH 命令：开启显示
	};

	// 如果配置了供电引脚 (supply-gpios)，则先上电
	if (config->supply.port) {
		gpio_pin_set_dt(&config->supply, 1); // 设置为高电平 (或激活电平)
		k_sleep(K_MSEC(SSD1306_SUPPLY_DELAY)); // 等待供电稳定
	}

	// 发送开启显示命令
	return ssd1306_write_bus(dev, cmd_buf, sizeof(cmd_buf), true);
}

// 暂停显示 (灭屏/消隐)
static int ssd1306_suspend(const struct device *dev)
{
	const struct ssd1306_config *config = dev->config;
	uint8_t cmd_buf[] = {
		SSD1306_DISPLAY_OFF, // AEH 命令：关闭显示
	};

	// 如果配置了供电引脚，则关闭供电
	if (config->supply.port) {
		gpio_pin_set_dt(&config->supply, 0); // 设置为低电平 (或非激活电平)
		k_sleep(K_MSEC(SSD1306_SUPPLY_DELAY)); // 等待关闭
	}

	// 发送关闭显示命令
	return ssd1306_write_bus(dev, cmd_buf, sizeof(cmd_buf), true);
}

// --- 像素数据写入实现 (SSD1306 默认模式) ---
// 该函数用于将像素数据写入 SSD1306/SSD1309 芯片
// x, y: 起始坐标
// desc: 缓冲区描述符 (包含宽度、高度、大小等)
// buf: 像素数据缓冲区
// buf_len: 缓冲区长度
static int ssd1306_write_default(const struct device *dev, const uint16_t x, const uint16_t y,
				 const struct display_buffer_descriptor *desc, const void *buf,
				 const size_t buf_len)
{
	const struct ssd1306_config *config = dev->config;
	uint8_t x_off = config->segment_offset; // 段偏移
	uint8_t cmd_buf[] = {
		// 1. 设置内存寻址模式
		SSD1306_SET_MEM_ADDRESSING_MODE, // 20H
		SSD1306_ADDRESSING_MODE,         // 使用设备树中配置的寻址模式 (例如水平寻址)
		// 2. 设置列地址范围
		SSD1306_SET_COLUMN_ADDRESS,      // 21H
		x + x_off,                       // 起始列地址 (加偏移)
		(x + desc->width - 1) + x_off,   // 结束列地址
		// 3. 设置页地址范围
		SSD1306_SET_PAGE_ADDRESS,        // 22H
		y / 8,                           // 起始页地址 (y 坐标除以 8)
		((y + desc->height) / 8 - 1)     // 结束页地址
	};

	// 1. 发送地址设置命令
	if (ssd1306_write_bus(dev, cmd_buf, sizeof(cmd_buf), true)) {
		LOG_ERR("Failed to write command");
		return -1;
	}

	// 2. 发送像素数据 (作为数据而不是命令)
	return ssd1306_write_bus(dev, (uint8_t *)buf, buf_len, false);
}

// --- 像素数据写入实现 (SH1106 兼容模式) ---
// SH1106 的寻址方式与 SSD1306 不同，通常需要按页（Page）逐页发送数据
static int ssd1306_write_sh1106(const struct device *dev, const uint16_t x, const uint16_t y,
				const struct display_buffer_descriptor *desc, const void *buf,
				const size_t buf_len)
{
	const struct ssd1306_config *config = dev->config;
	uint8_t x_offset = x + config->segment_offset;
	uint8_t cmd_buf[] = {
		SSD1306_SET_LOWER_COL_ADDRESS |
			(x_offset & SSD1306_SET_LOWER_COL_ADDRESS_MASK),
		SSD1306_SET_HIGHER_COL_ADDRESS |
			((x_offset >> 4) & SSD1306_SET_LOWER_COL_ADDRESS_MASK),
		SSD1306_SET_PAGE_START_ADDRESS | (y / 8)
	};
	uint8_t *buf_ptr = (uint8_t *)buf;

	for (uint8_t n = 0; n < desc->height / 8; n++) {
		cmd_buf[sizeof(cmd_buf) - 1] =
			SSD1306_SET_PAGE_START_ADDRESS | (n + (y / 8));
		LOG_HEXDUMP_DBG(cmd_buf, sizeof(cmd_buf), "cmd_buf");

		if (ssd1306_write_bus(dev, cmd_buf, sizeof(cmd_buf), true)) {
			return -1;
		}

		if (ssd1306_write_bus(dev, buf_ptr, desc->width, false)) {
			return -1;
		}

		buf_ptr = buf_ptr + desc->width;
		if (buf_ptr > ((uint8_t *)buf + buf_len)) {
			LOG_ERR("Exceeded buffer length");
			return -1;
		}
	}

	return 0;
}

// --- Zephyr Display API: 写入函数 (LVGL 调用此函数) ---
// 这是 Zephyr Display API 结构体中的 .write 接口实现
static int ssd1306_write(const struct device *dev, const uint16_t x, const uint16_t y,
			 const struct display_buffer_descriptor *desc, const void *buf)
{
	const struct ssd1306_config *config = dev->config;
	size_t buf_len;

	// 检查 pitch (行间距) 是否小于 width (宽度)
	if (desc->pitch < desc->width) {
		LOG_ERR("Pitch is smaller than width");
		return -1;
	}

	// 检查 y 坐标是否在有效的 Page 边界上 (必须是 8 的倍数)
	if ((y % 8U) != 0U) {
		LOG_ERR("Y coordinate is not a multiple of 8");
		return -1;
	}

	// 检查 x 坐标是否在有效的列边界上
	if ((x % 8U) != 0U) {
		LOG_ERR("X coordinate is not a multiple of 8");
		return -1;
	}

	// 计算有效缓冲区长度：像素总数 / 8 (单色屏)
	buf_len = MIN(desc->buf_size, (size_t)desc->height * desc->width / 8U);

	// 检查 x/y 坐标和宽高是否超出屏幕范围
	if (x + desc->width > config->width || y + desc->height > config->height) {
		LOG_ERR("Buffer dimensions exceed display limits");
		return -1;
	}

	// 根据兼容性标志调用不同的写入函数
	if (config->sh1106_compatible) {
		return ssd1306_write_sh1106(dev, x, y, desc, buf, buf_len);
	}

	// 默认调用 SSD1306 的写入函数
	return ssd1306_write_default(dev, x, y, desc, buf, buf_len);
}

// --- Zephyr Display API: 对比度设置 ---
static int ssd1306_set_contrast(const struct device *dev, const uint8_t contrast)
{
	uint8_t cmd_buf[] = {
		SSD1306_SET_CONTRAST_CTRL, // 81H 命令
		contrast,                  // 对比度参数 (0-255)
	};
	// 发送命令
	return ssd1306_write_bus(dev, cmd_buf, sizeof(cmd_buf), true);
}

// --- Zephyr Display API: 获取设备能力 (LVGL 调用此函数获取屏幕信息) ---
// 注意：此函数返回值必须是 void，以匹配 Zephyr API
static void ssd1306_get_capabilities(const struct device *dev,
				     struct display_capabilities *caps)
{
	const struct ssd1306_config *config = dev->config;
	struct ssd1306_data *data = dev->data;

	caps->x_resolution = config->width;         // 报告屏幕宽度
	caps->y_resolution = config->height;        // 报告屏幕高度
    // 报告支持的像素格式：MONO10 (1=亮) 和 MONO01 (0=亮)
	caps->supported_pixel_formats = PIXEL_FORMAT_MONO10 | PIXEL_FORMAT_MONO01;
	caps->current_pixel_format = data->pf;      // 报告当前使用的格式
    // 报告屏幕信息：单色，垂直分块寻址 (VTILED)
	caps->screen_info = SCREEN_INFO_MONO_VTILED;
	caps->current_orientation = DISPLAY_ORIENTATION_NORMAL; // 报告方向
}

// --- Zephyr Display API: 设置像素格式 (用于颜色反转) ---
static int ssd1306_set_pixel_format(const struct device *dev,
				    const enum display_pixel_format pf)
{
	struct ssd1306_data *data = dev->data;
	uint8_t cmd;
	int ret;

	// 如果格式未改变，则直接返回
	if (pf == data->pf) {
		return 0;
	}

    // PIXEL_FORMAT_MONO10 (1=亮): 对应 SET_NORMAL_DISPLAY (A6H)
	if (pf == PIXEL_FORMAT_MONO10) {
		cmd = SSD1306_SET_NORMAL_DISPLAY;
    // PIXEL_FORMAT_MONO01 (0=亮): 对应 SET_REVERSE_DISPLAY (A7H)
	} else if (pf == PIXEL_FORMAT_MONO01) {
		cmd = SSD1306_SET_REVERSE_DISPLAY;
	} else {
		LOG_WRN("Unsupported pixel format");
		return -ENOTSUP; // 不支持的格式
	}

	// 发送反转命令
	ret = ssd1306_write_bus(dev, &cmd, 1, true);
	if (ret) {
		return ret;
	}

	data->pf = pf; // 更新当前格式

	return 0;
}

// --- 驱动设备初始化 (发送初始化命令序列) ---
static int ssd1306_init_device(const struct device *dev)
{
	const struct ssd1306_config *config = dev->config;
	struct ssd1306_data *data = dev->data;

	uint8_t cmd_buf[] = {
		SSD1306_SET_ENTIRE_DISPLAY_OFF, // A4H: 输出遵循 RAM 内容
		// 根据设备树配置，设置初始显示模式
		(config->color_inversion ? SSD1306_SET_REVERSE_DISPLAY : SSD1306_SET_NORMAL_DISPLAY),
	};

	// 根据 color_inversion 标志设置初始像素格式
	data->pf = config->color_inversion ? PIXEL_FORMAT_MONO01 : PIXEL_FORMAT_MONO10;
	
    // 复位引脚操作 (如果配置了复位引脚)
	if (config->reset.port) {
		// 软件复位序列：拉低->延时->拉高->延时
		gpio_pin_set_dt(&config->reset, 0); // 拉低复位
		k_sleep(K_MSEC(1));                 // 延时
		gpio_pin_set_dt(&config->reset, 1); // 拉高复位
		k_sleep(K_MSEC(config->ready_time_ms)); // 再次延时等待芯片就绪
	}

	// 1. 关闭显示 (先保证关闭，再配置)
	if (ssd1306_suspend(dev)) { return -EIO; }

    // 2. 发送时序设置命令
	if (ssd1306_set_timing_setting(dev)) { return -EIO; }
    // 3. 发送硬件配置命令
	if (ssd1306_set_hardware_config(dev)) { return -EIO; }
    // 4. 发送面板方向命令
	if (ssd1306_set_panel_orientation(dev)) { return -EIO; }
    // 5. 发送电荷泵命令
	if (sh1106_set_charge_pump(dev)) { return -EIO; }
    // 6. 发送电流参考模式命令
	if (ssd1306_set_iref_mode(dev)) { return -EIO; }
    // 7. 发送初始显示模式命令 (A4H/A6H) RAM内容正常显示
	if (ssd1306_write_bus(dev, cmd_buf, ARRAY_SIZE(cmd_buf), true)) { return -EIO; }

    // 8. 设置初始对比度
	if (ssd1306_set_contrast(dev, SSD1306_DEFAULT_CONTRAST)) { return -EIO; }

    // 9. 恢复显示 (亮屏)
	ssd1306_resume(dev);

	return 0;
}

// --- Zephyr 设备初始化入口 ---
static int ssd1306_init(const struct device *dev)
{
	const struct ssd1306_config *config = dev->config;
	int ret;

	// 等待配置的 ready_time_ms (通常在上电后使用)
	k_sleep(K_TIMEOUT_ABS_MS(config->ready_time_ms));

	// 检查总线（I2C/SPI）是否就绪
	if (!ssd1306_bus_ready(dev)) {
		LOG_ERR("Bus device %s not ready!", config->bus_name(dev));
		return -EINVAL;
	}

	// 配置供电引脚 (supply-gpios)
	if (config->supply.port) {
		if (!device_is_ready(config->supply.port)) {
			LOG_ERR("Supply device %s not ready!", config->supply.port->name);
			return -EINVAL;
		}
		ret = gpio_pin_configure_dt(&config->supply, GPIO_OUTPUT_INACTIVE);
		if (ret < 0) {
			LOG_ERR("Could not configure supply pin: %d", ret);
			return ret;
		}
	}
    
	// 配置复位引脚 (reset-gpios)
	if (config->reset.port) {
		if (!device_is_ready(config->reset.port)) {
			LOG_ERR("Reset device %s not ready!", config->reset.port->name);
			return -EINVAL;
		}
		ret = gpio_pin_configure_dt(&config->reset, GPIO_OUTPUT_INACTIVE);
		if (ret < 0) {
			LOG_ERR("Could not configure reset pin: %d", ret);
			return ret;
		}
	}

	// 调用核心设备初始化序列
	if (ssd1306_init_device(dev)) {
		LOG_ERR("Failed to initialize device!");
		return -EIO;
	}

	LOG_INF("SSD1306 init success");

	return 0;
}

// --- Zephyr Display API 结构体定义 ---
// 将驱动函数的实现绑定到 Zephyr Display 驱动接口
static const struct display_driver_api ssd1306_driver_api = {
	.blanking_on = ssd1306_suspend,
	.blanking_off = ssd1306_resume,
	.write = ssd1306_write,           // 屏幕数据写入接口 (LVGL 调用此函数)
	.set_contrast = ssd1306_set_contrast,
	.get_capabilities = ssd1306_get_capabilities, // 获取设备能力接口 (LVGL 调用此函数)
	.set_pixel_format = ssd1306_set_pixel_format,
    // .read 接口未实现，通常用于获取像素信息，对于 OLED 很少使用
    // .get_mcu_interface 未实现，用于返回接口类型，Zephyr 可自行推断
};

// --- 设备树宏定义和设备注册 ---

// SPI 总线配置的宏 (此处省略了具体实现，但它会填充 SPI 相关的函数指针)
#define SSD1306_CONFIG_SPI(node_id) \
    // ... SPI 配置代码 ...

// I2C 总线配置的宏 (此处省略了具体实现，但它会填充 I2C 相关的函数指针)
#define SSD1306_CONFIG_I2C(node_id) \
	.bus = { .i2c = I2C_DT_SPEC_GET(node_id) }, \
	.bus_ready = ssd1306_bus_ready_i2c, \
	.write_bus = ssd1306_write_bus_i2c, \
	.bus_name = ssd1306_bus_name_i2c,

// SSD1306 设备定义的宏
#define SSD1306_DEFINE(node_id)                                                                    \
	/* 1. 定义数据结构体实例 (可读写) */                                                             \
	static struct ssd1306_data data##node_id;                                                  \
	/* 2. 定义配置结构体实例 (只读) */                                                             \
	static const struct ssd1306_config config##node_id = {                                     \
        /* 从设备树节点中读取所有属性并赋值给结构体成员 */                                                     \
		.reset = GPIO_DT_SPEC_GET_OR(node_id, reset_gpios, {0}),                           \
		.supply = GPIO_DT_SPEC_GET_OR(node_id, supply_gpios, {0}),                         \
		.height = DT_PROP(node_id, height),                                                \
		.width = DT_PROP(node_id, width),                                                  \
		.segment_offset = DT_PROP(node_id, segment_offset),                                \
		.page_offset = DT_PROP(node_id, page_offset),                                      \
		.display_offset = DT_PROP(node_id, display_offset),                                \
		.multiplex_ratio = DT_PROP(node_id, multiplex_ratio),                              \
		.segment_remap = DT_PROP(node_id, segment_remap),                                  \
		.com_invdir = DT_PROP(node_id, com_invdir),                                        \
		.com_sequential = DT_PROP(node_id, com_sequential),                                \
		.prechargep = DT_PROP(node_id, prechargep),                                        \
		.color_inversion = DT_PROP(node_id, inversion_on),                                 \
		.ssd1309_compatible = DT_NODE_HAS_COMPAT(node_id, solomon_ssd1309fb),              \
		.sh1106_compatible = DT_NODE_HAS_COMPAT(node_id, custom_sh1106),               		\
		.ready_time_ms = DT_PROP(node_id, ready_time_ms),                                  \
		.use_internal_iref = DT_PROP(node_id, use_internal_iref),                          \
        /* 根据总线类型 (SPI/I2C) 有条件地包含相应的配置宏 */                                                 \
		COND_CODE_1(DT_ON_BUS(node_id, spi), (SSD1306_CONFIG_SPI(node_id)),                \
			    (SSD1306_CONFIG_I2C(node_id)))                                         \
	};                                                                                         \
	/* 3. 注册 Zephyr 设备 */                                                                  \
	/* DEVICE_DT_DEFINE(节点ID, 初始化函数, PM管理句柄, 数据结构, 配置结构, 初始化阶段, 初始化优先级, 驱动API) */ \
	DEVICE_DT_DEFINE(node_id, ssd1306_init, NULL, &data##node_id, &config##node_id,            \
			 POST_KERNEL, CONFIG_DISPLAY_INIT_PRIORITY, &ssd1306_driver_api);

// 遍历设备树中所有状态为 "okay" 的节点，如果兼容性字符串匹配，则调用 SSD1306_DEFINE 宏注册驱动
// DT_FOREACH_STATUS_OKAY(solomon_ssd1306fb, SSD1306_DEFINE)
// 提示：如果你使用的是自定义的 "custom,sh1106"，你需要在项目本地的宏中添加一行
DT_FOREACH_STATUS_OKAY(custom_sh1106, SSD1306_DEFINE)