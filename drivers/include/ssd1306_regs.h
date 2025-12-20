/*
 * Copyright (c) 2018 Phytec Messtechnik GmbH
 *
 * SPDX-License-Identifier: Apache-2.0
 */


#ifndef __SSD1306_REGS_H__
#define __SSD1306_REGS_H__

/* 控制字节定义 */
#define SSD1306_CONTROL_ALL_BYTES_CMD		0x00 /* 后面所有字节都是命令 */
#define SSD1306_CONTROL_ALL_BYTES_DATA		0x40 /* 后面所有字节都是数据 */
#define SSD1306_CONTROL_BYTE_CMD		    0x80 /* 下一个字节是命令 */
#define SSD1306_CONTROL_BYTE_DATA		    0xC0 /* 下一个字节是数据 */

/* 读取状态命令 */
#define SSD1306_READ_STATUS_MASK		0xc0
#define SSD1306_READ_STATUS_BUSY		0x80
#define SSD1306_READ_STATUS_ON			0x40

/* 基础命令 */
#define SSD1306_SET_CONTRAST_CTRL		    0x81 /* 设置对比度 (双字节命令，后跟亮度值 0-FF) */
#define SSD1306_SET_ENTIRE_DISPLAY_OFF		0xA4 /* 输出遵循 RAM 内容 */
#define SSD1306_SET_ENTIRE_DISPLAY_ON		0xA5 /* 全屏点亮 (忽略 RAM) */
#define SSD1306_SET_NORMAL_DISPLAY		    0xA6 /* 正常显示 (1点亮, 0熄灭) */
#define SSD1306_SET_REVERSE_DISPLAY 		0xA7 /* 反显显示 (0点亮, 1熄灭) */
#define SSD1306_DISPLAY_OFF			        0xAE /* 关闭显示屏 (休眠模式) */
#define SSD1306_DISPLAY_ON			        0xAF /* 开启显示屏 */

# if 1
/* * 地址设置命令表 (Addressing Setting Command Table)
 * 这些命令用于控制显示数据 RAM (GDDRAM) 的寻址方式
 */

/* 1. 设置起始列地址的低 4 位 (仅用于“页寻址模式”) */
/* 实际发送时，通过 (0x00 | column_low_4_bits) 使用 */
#define SSD1306_SET_LOWER_COL_ADDRESS       0x00 
#define SSD1306_SET_LOWER_COL_ADDRESS_MASK  0x0f

/* 2. 设置起始列地址的高 4 位 (仅用于“页寻址模式”) */
/* 实际发送时，通过 (0x10 | column_high_4_bits) 使用 */
#define SSD1306_SET_HIGHER_COL_ADDRESS      0x10
#define SSD1306_SET_HIGHER_COL_ADDRESS_MASK 0x0f

/* 3. 设置内存寻址模式 (这是一个双字节命令：先发 0x20，再发模式值) */
#define SSD1306_SET_MEM_ADDRESSING_MODE     0x20 
#define SSD1306_SET_MEM_ADDRESSING_HORIZONTAL   0x00 /* 水平寻址：刷完一行自动换到下一行起始 */
#define SSD1306_SET_MEM_ADDRESSING_VERTICAL 0x01   /* 垂直寻址：刷完一列自动换到下一列起始 */
#define SSD1306_SET_MEM_ADDRESSING_PAGE     0x02     /* 页寻址：刷完一行后，指针回到本行起始，不自动换行 */

/* 4. 设置列地址范围 (三字节命令：0x21 + 起始列 + 结束列) */
/* 注意：此命令在 SSD1306 的水平/垂直模式下有效，但在 SH1106 上通常无效 */
#define SSD1306_SET_COLUMN_ADDRESS          0x21 

/* 5. 设置页地址范围 (三字节命令：0x22 + 起始页 + 结束页) */
/* 注意：同上，主要用于 SSD1306 的自动换行模式 */
#define SSD1306_SET_PAGE_ADDRESS            0x22 

/* 6. 设置页寻址模式下的起始页地址 (0xB0 ~ 0xB7) */
/* 实际发送时，通过 (0xB0 | page_index) 使用，i 范围 0-7 代表 8 个页 */
#define SSD1306_SET_PAGE_START_ADDRESS      0xb0
#define SSD1306_SET_PAGE_START_ADDRESS_MASK 0x07

#else
/* 地址设置命令 */
#define SSD1306_SET_MEM_ADDRESSING_MODE		0x20 /* 设置内存寻址模式 (双字节命令) */
#define SSD1306_SET_MEM_ADDRESSING_HORIZONTAL	0x00 /* 水平寻址 */
#define SSD1306_SET_MEM_ADDRESSING_VERTICAL	    0x01 /* 垂直寻址 */
#define SSD1306_SET_MEM_ADDRESSING_PAGE		    0x02 /* 页寻址 */
#define SSD1306_SET_COLUMN_ADDRESS			0x21 /* 设置列起始和结束地址 (SSD1306 专用) */
#define SSD1306_SET_PAGE_ADDRESS            0x22 /* 设置页起始和结束地址 */
#define SSD1306_SET_PAGE_START_ADDRESS      0xB0 /* 设置页寻址模式下的起始页地址 (0xB0~0xB7) */
#define SSD1306_SET_PAGE_START_ADDRESS_MASK	0x07 /* 页寻址模式下，起始页地址的低 3 位 */
#define SSD1306_SET_LOW_COL_ADDR		    0x00 /* 设置起始列地址低4位 (0x00~0x0F) */
#define SSD1306_SET_HIGH_COL_ADDR		    0x10 /* 设置起始列地址高4位 (0x10~0x1F) */
#endif

# if 0
/*
 * Hardware Configuration Command Table
 */
#define SSD1306_SET_START_LINE			0x40
#define SSD1306_SET_START_LINE_MASK		0x3f

#define SSD1306_SET_SEGMENT_MAP_NORMAL		0xa0
#define SSD1306_SET_SEGMENT_MAP_REMAPED		0xa1

#define SSD1306_SET_MULTIPLEX_RATIO		0xa8 /* double byte command */

#define SSD1306_SET_COM_OUTPUT_SCAN_NORMAL	0xc0
#define SSD1306_SET_COM_OUTPUT_SCAN_FLIPPED	0xc8

#define SSD1306_SET_DISPLAY_OFFSET		0xd3 /* double byte command */

#define SSD1306_SET_PADS_HW_CONFIG		0xda /* double byte command */
#define SSD1306_SET_PADS_HW_SEQUENTIAL		0x02
#define SSD1306_SET_PADS_HW_ALTERNATIVE         0x12

#define SSD1306_SET_IREF_MODE			0xad
#define SSD1306_SET_IREF_MODE_INTERNAL		0x30
#define SSD1306_SET_IREF_MODE_EXTERNAL		0x00
#endif

/* 硬件配置命令 */
#define SSD1306_SET_START_LINE  	    	0x40 /* 设置显示起始行 (0x40~0x7F) */
#define SSD1306_SET_SEGMENT_REMAP_NORMAL	0xA0 /* 段重映射：col 0 映射到 SEG0 */
#define SSD1306_SET_SEGMENT_REMAP_FLIPPED	0xA1 /* 段重映射：col 127 映射到 SEG0 (左右反转) */
#define SSD1306_SET_MULTIPLEX_RATIO		    0xA8 /* 设置多路复用率 (双字节命令) */
#define SSD1306_SET_SEGMENT_MAP_NORMAL		0xA0 /* 段重映射：映射关系：列地址 0 对应 SEG0 引脚 */
#define SSD1306_SET_SEGMENT_MAP_REMAPED		0xA1 /* 段重映射：映射关系：列地址 0 对应 SEG127 引脚 */
#define SSD1306_SET_COM_OUTPUT_SCAN_NORMAL	0xC0 /* COM 扫描方向：从 COM0 到 COM[N-1] */
#define SSD1306_SET_COM_OUTPUT_SCAN_FLIPPED	0xC8 /* COM 扫描方向：从 COM[N-1] 到 COM0 (上下反转) */
#define SSD1306_SET_DISPLAY_OFFSET		    0xD3 /* 设置显示偏移 (双字节命令) */
#define SSD1306_SET_PADS_HW_CONFIG		    0xDA /* 设置 COM 引脚硬件配置 (双字节命令) */
#define SSD1306_SET_PADS_HW_SEQUENTIAL		0x02 /* COM 引脚顺序配置 */
#define SSD1306_SET_PADS_HW_ALTERNATIVE     0x12 /* COM 引脚交替配置 */
#define SSD1306_SET_IREF_MODE			    0xAD /* 设置内部参考模式 */
#define SSD1306_SET_IREF_MODE_INTERNAL		0x30
#define SSD1306_SET_IREF_MODE_EXTERNAL		0x00


/* 时序与驱动设置 */
#define SSD1306_SET_CLOCK_DIV_RATIO		    0xD5 /* 设置时钟分频/振荡频率 (双字节命令) */
#define SSD1306_SET_CHARGE_PERIOD		    0xD9 /* 设置预充电周期 (双字节命令) */
#define SSD1306_SET_VCOM_DESELECT_LEVEL		0xDB /* 设置 VCOMH 去激励电压 (双字节命令) */
#define SSD1306_NOP				            0xE3 /* 空指令 */

/* 电荷泵命令 */
/* --- SSD1306 特有命令 --- */
#define SSD1306_SET_CHARGE_PUMP_ON		    0x8D
#define SSD1306_SET_CHARGE_PUMP_ON_DISABLED	0x10
#define SSD1306_SET_CHARGE_PUMP_ON_ENABLED	0x14

#define SSD1306_SET_PUMP_VOLTAGE_64		0x30
#define SSD1306_SET_PUMP_VOLTAGE_74		0x31
#define SSD1306_SET_PUMP_VOLTAGE_80		0x32
#define SSD1306_SET_PUMP_VOLTAGE_90		0x33

/* --- SH1106 特有命令 --- */
#define SH1106_SET_DCDC_MODE                0xAD /* SH1106 电荷泵/DCDC 设置 (双字节) */
#define SH1106_SET_DCDC_DISABLED            0x8A /* 关闭内部电荷泵 */
#define SH1106_SET_DCDC_ENABLED             0x8B /* 开启内部分压/电荷泵 */
#define SH1106_COLUMN_OFFSET                2    /* 关键：SH1106 通常有 2 像素的列偏移 */

/* --- 屏幕对比度 --- */
#define SSD1306_DEFAULT_CONTRAST		0xFF /* 对比度默认值，最大值 */

/*
 * Read modify write
 */
#define SSD1306_READ_MODIFY_WRITE_START		0xe0
#define SSD1306_READ_MODIFY_WRITE_END		0xee

/* time constants in ms */
#define SSD1306_RESET_DELAY			    1
#define SSD1306_SUPPLY_DELAY			100

#endif
