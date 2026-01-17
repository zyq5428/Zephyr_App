/*
 * Copyright (c) 2017 Jan Van Winkel <jan.van_winkel@dxplore.eu>
 * Copyright (c) 2019 Nordic Semiconductor ASA
 * Copyright (c) 2019 Marc Reilly
 * Copyright (c) 2019 PHYTEC Messtechnik GmbH
 * Copyright (c) 2020 Endian Technologies AB
 * Copyright (c) 2022 Basalte bv
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT custom_st7789v

#include "st7789v.h"

#include <zephyr/device.h>
#include <zephyr/drivers/mipi_dbi.h>
#include <zephyr/pm/device.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(st7789_direct, LOG_LEVEL_INF);

#define CONFIG_ST7789V_RGB565

struct st7789v_config {
	struct spi_dt_spec spi;       // SPI 总线配置
    struct gpio_dt_spec dc_gpio;  // DC 引脚
    struct gpio_dt_spec rst_gpio; // Reset 引脚
    struct gpio_dt_spec bl_gpio;  // Backlight 引脚 (可选)
	struct pwm_dt_spec bl_pwm;  // Backlight-pwm 引脚 (可选)
	uint8_t vcom;
	uint8_t gctrl;
	bool vdv_vrh_enable;
	uint8_t vrh_value;
	uint8_t vdv_value;
	uint8_t mdac;
	uint8_t gamma;
	uint8_t colmod;
	uint8_t lcm;
	bool inversion_on;
	uint8_t porch_param[5];
	uint8_t cmd2en_param[4];
	uint8_t pwctrl1_param[2];
	uint8_t pvgam_param[14];
	uint8_t nvgam_param[14];
	uint8_t ram_param[2];
	uint8_t rgb_param[3];
	uint16_t height;
	uint16_t width;
	uint8_t ready_time_ms;
};

struct st7789v_data {
	uint16_t x_offset;
	uint16_t y_offset;
};

#ifdef CONFIG_ST7789V_RGB888
#define ST7789V_PIXEL_SIZE 3u
#else
#define ST7789V_PIXEL_SIZE 2u
#endif

static void st7789v_set_lcd_margins(const struct device *dev,
				    uint16_t x_offset, uint16_t y_offset)
{
	struct st7789v_data *data = dev->data;

	data->x_offset = x_offset;
	data->y_offset = y_offset;
}

/**
 * @brief 辅助函数：通过 SPI 发送原始数据
 */
static int st7789v_spi_write(const struct device *dev, const uint8_t *data, size_t len)
{
    const struct st7789v_config *config = dev->config;
    struct spi_buf buf = {.buf = (void *)data, .len = len};
    struct spi_buf_set tx = {.buffers = &buf, .count = 1};

    // 使用 Zephyr 标准 SPI 写函数
    return spi_write_dt(&config->spi, &tx);
}

/**
 * @brief 发送命令到屏幕
 */
static int st7789v_write_cmd(const struct device *dev, uint8_t cmd)
{
    const struct st7789v_config *config = dev->config;
    // DC 拉低：表示当前发送的是命令
    gpio_pin_set_dt(&config->dc_gpio, 0);
    return st7789v_spi_write(dev, &cmd, 1);
}

/**
 * @brief 发送数据到屏幕
 */
static int st7789v_write_data(const struct device *dev, const uint8_t *data, size_t len)
{
    const struct st7789v_config *config = dev->config;
    if (len == 0) return 0;
    // DC 拉高：表示当前发送的是数据
    gpio_pin_set_dt(&config->dc_gpio, 1);
    return st7789v_spi_write(dev, data, len);
}

/**
 * @brief 综合传输函数：先发命令，再发数据（如果 tx_count > 0）
 */
static int st7789v_transmit(const struct device *dev, uint8_t cmd,
			    const uint8_t *tx_data, size_t tx_count)
{
	int ret = st7789v_write_cmd(dev, cmd);
	if (ret < 0) return ret;

	if (tx_count > 0) {
		ret = st7789v_write_data(dev, tx_data, tx_count);
	}
	return ret;
}

static int st7789v_exit_sleep(const struct device *dev)
{
	int ret;

	ret = st7789v_transmit(dev, ST7789V_CMD_SLEEP_OUT, NULL, 0);
	if (ret < 0) {
		return ret;
	}

	k_sleep(K_MSEC(120));
	return ret;
}

/* 简单的屏幕复位逻辑 */
static int st7789v_reset_display(const struct device *dev)
{
	const struct st7789v_config *config = dev->config;
	int ret = 0;

	LOG_DBG("Resetting display");

	k_sleep(K_MSEC(1));
	if (config->rst_gpio.port != NULL) {
		gpio_pin_set_dt(&config->rst_gpio, 0); // 释放复位
		k_sleep(K_MSEC(20));
		gpio_pin_set_dt(&config->rst_gpio, 1); // 拉低复位引脚
		k_sleep(K_MSEC(50));
		gpio_pin_set_dt(&config->rst_gpio, 0); // 拉高恢复
		k_sleep(K_MSEC(150));
	} else {
		// 硬件复位引脚不可用时，使用软件复位命令
		ret = st7789v_transmit(dev, ST7789V_CMD_SW_RESET, NULL, 0);
		if (ret < 0) {
			return ret;
		}
		k_sleep(K_MSEC(120));
	}

	return ret;
}

static int st7789v_blanking_on(const struct device *dev)
{
	return st7789v_transmit(dev, ST7789V_CMD_DISP_OFF, NULL, 0);
}

static int st7789v_blanking_off(const struct device *dev)
{
	return st7789v_transmit(dev, ST7789V_CMD_DISP_ON, NULL, 0);
}

static int st7789v_set_mem_area(const struct device *dev, const uint16_t x,
				 const uint16_t y, const uint16_t w, const uint16_t h)
{
	struct st7789v_data *data = dev->data;
	uint16_t spi_data[2];

	uint16_t ram_x = x + data->x_offset;
	uint16_t ram_y = y + data->y_offset;

	int ret;

	spi_data[0] = sys_cpu_to_be16(ram_x);
	spi_data[1] = sys_cpu_to_be16(ram_x + w - 1);
	ret = st7789v_transmit(dev, ST7789V_CMD_CASET, (uint8_t *)&spi_data[0], 4);
	if (ret < 0) {
		return ret;
	}

	spi_data[0] = sys_cpu_to_be16(ram_y);
	spi_data[1] = sys_cpu_to_be16(ram_y + h - 1);
	return st7789v_transmit(dev, ST7789V_CMD_RASET, (uint8_t *)&spi_data[0], 4);
}

/**
 * @brief 强壮版写入函数：支持处理非连续内存 (Pitch > Width)
 * * @param dev   设备句柄
 * @param x     起始横坐标
 * @param y     起始纵坐标
 * @param desc  缓冲区描述符（包含宽度、高度、跨度等）
 * @param buf   实际像素数据指针
 */
static int st7789v_write(const struct device *dev,
             const uint16_t x,
             const uint16_t y,
             const struct display_buffer_descriptor *desc,
             const void *buf)
{
    int ret;
    const uint8_t *pixel_data = (const uint8_t *)buf;
    
    /* 1. 安全检查：确保跨度（Pitch）合法 */
    if (desc->pitch < desc->width) {
        LOG_ERR("Pitch (%d) cannot be smaller than width (%d)", desc->pitch, desc->width);
        return -EINVAL;
    }

    /* 2. 设置屏幕窗口：告诉屏幕我们要画在哪个矩形区域 */
    ret = st7789v_set_mem_area(dev, x, y, desc->width, desc->height);
    if (ret < 0) {
        return ret;
    }

    /* 3. 发送写 RAM 命令 (0x2C) */
    ret = st7789v_write_cmd(dev, ST7789V_CMD_RAMWR);
    if (ret < 0) {
        return ret;
    }

    /* 4. 数据传输逻辑：判断是否需要分行发送 */
    if (desc->pitch == desc->width) {
        /* 【情况 A：内存连续】
         * 跨度等于宽度，说明内存里没有“空隙”。
         * 我们可以把整个缓冲区作为一整块，一次性通过 SPI 发送出去，效率最高。
         */
        ret = st7789v_write_data(dev, pixel_data, desc->buf_size);
    } else {
        /* 【情况 B：内存不连续 (Pitch > Width)】
         * 说明每一行像素后面跟着一段无用的“填充数据”。
         * 我们必须通过循环，每一行只发送有效宽度的数据，然后跳过填充部分。
         */
        for (uint16_t row = 0; row < desc->height; row++) {
            /* 发送当前行的有效像素数据 */
            size_t row_bytes = desc->width * ST7789V_PIXEL_SIZE;
            ret = st7789v_write_data(dev, pixel_data, row_bytes);
            if (ret < 0) {
                return ret;
            }

            /* 将指针移动到下一行的起始位置：
             * 指针跳过的是整个 Pitch 的长度
             */
            pixel_data += (desc->pitch * ST7789V_PIXEL_SIZE);
        }
    }

    return ret;
}

static void st7789v_get_capabilities(const struct device *dev,
			      struct display_capabilities *capabilities)
{
	const struct st7789v_config *config = dev->config;

	memset(capabilities, 0, sizeof(struct display_capabilities));
	capabilities->x_resolution = config->width;
	capabilities->y_resolution = config->height;

#ifdef CONFIG_ST7789V_RGB565
	capabilities->supported_pixel_formats = PIXEL_FORMAT_RGB_565;
	capabilities->current_pixel_format = PIXEL_FORMAT_RGB_565;
#elif CONFIG_ST7789V_BGR565
	capabilities->supported_pixel_formats = PIXEL_FORMAT_BGR_565;
	capabilities->current_pixel_format = PIXEL_FORMAT_BGR_565;
#else
	capabilities->supported_pixel_formats = PIXEL_FORMAT_RGB_888;
	capabilities->current_pixel_format = PIXEL_FORMAT_RGB_888;
#endif
	capabilities->current_orientation = DISPLAY_ORIENTATION_NORMAL;
}

static int st7789v_set_pixel_format(const struct device *dev,
			     const enum display_pixel_format pixel_format)
{
#ifdef CONFIG_ST7789V_RGB565
	if (pixel_format == PIXEL_FORMAT_RGB_565) {
#elif CONFIG_ST7789V_BGR565
	if (pixel_format == PIXEL_FORMAT_BGR_565) {
#else
	if (pixel_format == PIXEL_FORMAT_RGB_888) {
#endif
		return 0;
	}
	LOG_ERR("Pixel format change not implemented");
	return -ENOTSUP;
}

static int st7789v_set_orientation(const struct device *dev,
			    const enum display_orientation orientation)
{
	if (orientation == DISPLAY_ORIENTATION_NORMAL) {
		return 0;
	}
	LOG_ERR("Changing display orientation not implemented");
	return -ENOTSUP;
}

static int st7789v_lcd_init(const struct device *dev)
{
	struct st7789v_data *data = dev->data;
	const struct st7789v_config *config = dev->config;
	uint8_t tmp;
	int ret = 0;

	st7789v_set_lcd_margins(dev, data->x_offset,
				data->y_offset);

	ret = st7789v_transmit(dev, ST7789V_CMD_CMD2EN,
			       (uint8_t *)config->cmd2en_param,
			       sizeof(config->cmd2en_param));
	if (ret < 0) {
		return ret;
	}

	ret = st7789v_transmit(dev, ST7789V_CMD_PORCTRL,
			       (uint8_t *)config->porch_param,
			       sizeof(config->porch_param));
	if (ret < 0) {
		return ret;
	}

	/* Digital Gamma Enable, default disabled */
	tmp = 0x00;
	ret = st7789v_transmit(dev, ST7789V_CMD_DGMEN, &tmp, 1);
	if (ret < 0) {
		return ret;
	}

	/* Frame Rate Control in Normal Mode, default value */
	tmp = 0x0f;
	ret = st7789v_transmit(dev, ST7789V_CMD_FRCTRL2, &tmp, 1);
	if (ret < 0) {
		return ret;
	}

	tmp = config->gctrl;
	ret = st7789v_transmit(dev, ST7789V_CMD_GCTRL, &tmp, 1);
	if (ret < 0) {
		return ret;
	}

	tmp = config->vcom;
	ret = st7789v_transmit(dev, ST7789V_CMD_VCOMS, &tmp, 1);
	if (ret < 0) {
		return ret;
	}

	if (config->vdv_vrh_enable) {
		tmp = 0x01;
		ret = st7789v_transmit(dev, ST7789V_CMD_VDVVRHEN, &tmp, 1);
		if (ret < 0) {
			return ret;
		}

		tmp = config->vrh_value;
		ret = st7789v_transmit(dev, ST7789V_CMD_VRH, &tmp, 1);
		if (ret < 0) {
			return ret;
		}

		tmp = config->vdv_value;
		ret = st7789v_transmit(dev, ST7789V_CMD_VDS, &tmp, 1);
		if (ret < 0) {
			return ret;
		}
	}

	ret = st7789v_transmit(dev, ST7789V_CMD_PWCTRL1,
			       (uint8_t *)config->pwctrl1_param,
			       sizeof(config->pwctrl1_param));
	if (ret < 0) {
		return ret;
	}

	/* Memory Data Access Control */
	tmp = config->mdac;
	ret = st7789v_transmit(dev, ST7789V_CMD_MADCTL, &tmp, 1);
	if (ret < 0) {
		return ret;
	}

	/* Interface Pixel Format */
	tmp = config->colmod;
	ret = st7789v_transmit(dev, ST7789V_CMD_COLMOD, &tmp, 1);
	if (ret < 0) {
		return ret;
	}

	tmp = config->lcm;
	ret = st7789v_transmit(dev, ST7789V_CMD_LCMCTRL, &tmp, 1);
	if (ret < 0) {
		return ret;
	}

	tmp = config->gamma;
	ret = st7789v_transmit(dev, ST7789V_CMD_GAMSET, &tmp, 1);
	if (ret < 0) {
		return ret;
	}

	if (config->inversion_on) {
		ret = st7789v_transmit(dev, ST7789V_CMD_INV_ON, NULL, 0);
	} else {
		ret = st7789v_transmit(dev, ST7789V_CMD_INV_OFF, NULL, 0);
	}
	if (ret < 0) {
		return ret;
	}

	ret = st7789v_transmit(dev, ST7789V_CMD_PVGAMCTRL,
			       (uint8_t *)config->pvgam_param,
			       sizeof(config->pvgam_param));
	if (ret < 0) {
		return ret;
	}

	ret = st7789v_transmit(dev, ST7789V_CMD_NVGAMCTRL,
			       (uint8_t *)config->nvgam_param,
			       sizeof(config->nvgam_param));
	if (ret < 0) {
		return ret;
	}

	ret = st7789v_transmit(dev, ST7789V_CMD_RAMCTRL,
			       (uint8_t *)config->ram_param,
			       sizeof(config->ram_param));
	if (ret < 0) {
		return ret;
	}

	ret = st7789v_transmit(dev, ST7789V_CMD_RGBCTRL,
			       (uint8_t *)config->rgb_param,
			       sizeof(config->rgb_param));
	return ret;
}

/**
 * @brief 内部函数：设置背光状态
 * @param brightness 亮度值 (0-100)
 */
static int st7789v_set_backlight(const struct device *dev, uint8_t brightness)
{
    const struct st7789v_config *config = dev->config;

    /* 优先级 1：检查 PWM 是否有效 */
    if (pwm_is_ready_dt(&config->bl_pwm)) {
		// 将 brightness 限制在 0 到 255 之间
		uint8_t safe_brightness = CLAMP(brightness, 0, 255);
		uint32_t pulse = (config->bl_pwm.period * safe_brightness) / 255;
        int ret = pwm_set_dt(&config->bl_pwm, config->bl_pwm.period, pulse);
        // LOG_INF("使用 PWM 调节亮度: %d%%", brightness);
        return ret; // 处理完毕，直接退出
    }

    /* 优先级 2：检查 GPIO 是否有效 (只有 PWM 无效时才会走到这里) */
    if (config->bl_gpio.port != NULL) {
		if (!gpio_is_ready_dt(&config->bl_gpio)) {
            return -ENODEV;
        }
        // GPIO 只有开关，通常亮度 > 0 就打开
        int ret = gpio_pin_set_dt(&config->bl_gpio, brightness > 0 ? 1 : 0);
        // LOG_INF("使用 GPIO 控制背光: %s", brightness > 0 ? "开启" : "关闭");
        return ret;
    }

    /* 如果走到这里，说明用户既没配 PWM 也没配 GPIO */
    // LOG_WRN("未发现任何背光控制配置");
	return -ENOTSUP; // 既没 PWM 也没 GPIO，返回“不支持”
}

/**
 * @brief 屏幕初始化流程
 */
static int st7789v_init(const struct device *dev)
{
	const struct st7789v_config *config = dev->config;
	int ret = 0;

	// 检查硬件是否就绪
	if (!spi_is_ready_dt(&config->spi)) {
		LOG_ERR("SPI device not ready");
		return -ENODEV;
	}
	if (!gpio_is_ready_dt(&config->dc_gpio)) {
		LOG_ERR("DC GPIO not ready");
		return -ENODEV;
	}
	if (!gpio_is_ready_dt(&config->rst_gpio)) {
		LOG_ERR("RST GPIO not ready");
		return -ENODEV;
	}
	
	k_sleep(K_TIMEOUT_ABS_MS(config->ready_time_ms));

	// 初始化 GPIO 引脚
	gpio_pin_configure_dt(&config->dc_gpio, GPIO_OUTPUT_INACTIVE);
	
	if (config->rst_gpio.port) {
		gpio_pin_configure_dt(&config->rst_gpio, GPIO_OUTPUT_INACTIVE);
	}

	/* 初始化时开启背光 (假设设为100%) */
    st7789v_set_backlight(dev, 100);

	// 设备复位
	ret = st7789v_reset_display(dev);
	if (ret < 0) {
		LOG_ERR("Failed to reset display (%d)", ret);
		return ret;
	}
	
	ret = st7789v_blanking_on(dev);
	if (ret < 0) {
		LOG_ERR("Failed to turn blanking on (%d)", ret);
		return ret;
	}

	ret = st7789v_lcd_init(dev);
	if (ret < 0) {
		LOG_ERR("Failed to init display (%d)", ret);
		return ret;
	}

	ret = st7789v_exit_sleep(dev);
	if (ret < 0) {
		LOG_ERR("Failed to exit the sleep mode (%d)", ret);
		return ret;
	}

	// 重要：退出休眠后必须等待 120ms 
	k_sleep(K_MSEC(120));

	// 明确开启显示 (发送指令 0x29)
    // 注意：st7789v_blanking_off 内部封装了 ST7789V_CMD_DISP_ON
	ret = st7789v_blanking_off(dev);
	if (ret < 0) {
		LOG_ERR("Failed to turn on display (%d)", ret);
		return ret;
	}

	LOG_DBG("ST7789V 驱动初始化序列全部发送成功");
	return ret;
}

#ifdef CONFIG_PM_DEVICE
static int st7789v_pm_action(const struct device *dev,
			     enum pm_device_action action)
{
	int ret;

	switch (action) {
	case PM_DEVICE_ACTION_RESUME:
		ret = st7789v_exit_sleep(dev);
		break;
	case PM_DEVICE_ACTION_SUSPEND:
		ret = st7789v_transmit(dev, ST7789V_CMD_SLEEP_IN, NULL, 0);
		break;
	default:
		ret = -ENOTSUP;
		break;
	}

	return ret;
}
#endif /* CONFIG_PM_DEVICE */

static DEVICE_API(display, st7789v_api) = {
	.blanking_on = st7789v_blanking_on,
	.blanking_off = st7789v_blanking_off,
	.write = st7789v_write,
	.set_brightness = st7789v_set_backlight,
	.get_capabilities = st7789v_get_capabilities,
	.set_pixel_format = st7789v_set_pixel_format,
	.set_orientation = st7789v_set_orientation,
};

#define ST7789V_INIT(inst)								\
	static const struct st7789v_config st7789v_config_ ## inst = {			\
		.spi = SPI_DT_SPEC_INST_GET(inst, SPI_OP_MODE_MASTER | SPI_WORD_SET(8)), \
		.dc_gpio = GPIO_DT_SPEC_INST_GET(inst, dc_gpios),			\
		.rst_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, reset_gpios, {0}),		\
		.bl_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, bl_gpios, {0}),		\
		.bl_pwm = PWM_DT_SPEC_INST_GET_OR(inst, {0}),		\
		.vcom = DT_INST_PROP(inst, vcom),					\
		.gctrl = DT_INST_PROP(inst, gctrl),					\
		.vdv_vrh_enable = (DT_INST_NODE_HAS_PROP(inst, vrhs)			\
					&& DT_INST_NODE_HAS_PROP(inst, vdvs)),		\
		.vrh_value = DT_INST_PROP_OR(inst, vrhs, 0),				\
		.vdv_value = DT_INST_PROP_OR(inst, vdvs, 0),				\
		.mdac = DT_INST_PROP(inst, mdac),					\
		.gamma = DT_INST_PROP(inst, gamma),					\
		.colmod = DT_INST_PROP(inst, colmod),					\
		.lcm = DT_INST_PROP(inst, lcm),						\
		.inversion_on = !DT_INST_PROP(inst, inversion_off),			\
		.porch_param = DT_INST_PROP(inst, porch_param),				\
		.cmd2en_param = DT_INST_PROP(inst, cmd2en_param),			\
		.pwctrl1_param = DT_INST_PROP(inst, pwctrl1_param),			\
		.pvgam_param = DT_INST_PROP(inst, pvgam_param),				\
		.nvgam_param = DT_INST_PROP(inst, nvgam_param),				\
		.ram_param = DT_INST_PROP(inst, ram_param),				\
		.rgb_param = DT_INST_PROP(inst, rgb_param),				\
		.width = DT_INST_PROP(inst, width),					\
		.height = DT_INST_PROP(inst, height),					\
		.ready_time_ms = DT_INST_PROP(inst, ready_time_ms),			\
	};										\
											\
	static struct st7789v_data st7789v_data_ ## inst = {				\
		.x_offset = DT_INST_PROP(inst, x_offset),				\
		.y_offset = DT_INST_PROP(inst, y_offset),				\
	};										\
											\
	PM_DEVICE_DT_INST_DEFINE(inst, st7789v_pm_action);				\
											\
	DEVICE_DT_INST_DEFINE(inst, &st7789v_init, PM_DEVICE_DT_INST_GET(inst),		\
			&st7789v_data_ ## inst, &st7789v_config_ ## inst,		\
			POST_KERNEL, CONFIG_DISPLAY_INIT_PRIORITY,			\
			&st7789v_api);

DT_INST_FOREACH_STATUS_OKAY(ST7789V_INIT)
