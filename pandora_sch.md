| 模块类别 | 设备/功能 | 信号名称 | 引脚 (Pin) | STM32 端口 | I2C 地址 | AF (复用功能) | 备注/配置 |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **时钟 (HSE)** | 外部晶振 | OSC\_IN/OUT | PH0/PH1 | GPIO H0/H1 | N/A | ANALOG | 8MHz 外部晶振源 |
| **调试/系统** | USART1 (Console) | TX/RX | PA9/PA10 | GPIO A9/A10 | N/A | AF7 | 调试串口 |
| | USB OTG FS | DM/DP | PA11/PA12 | GPIO A11/A12 | N/A | AF10 | USB 全速模式 |
| **指示灯** | LED\_R/G/B | - | PE7/PE8/PE9 | GPIO E7/E8/E9 | N/A | - | **低电平**有效 (Active Low) |
| **按键** | KEY0/KEY1/KEY2 | - | PD10/PD9/PD8 | GPIO D10/D9/D8 | N/A | - | **低电平**有效，需上拉 |
| | WK\_UP | - | PC13 | GPIO C13 | N/A | - | **低电平**有效，需上拉 |
| **I2C 传感器** | I2C3 SCL/SDA | - | PC0/PC1 | GPIO C0/C1 | N/A | AF4 | 传感器总线 |
| | ICM-20608 | **中断 INT** | PC4 | GPIO C4 | **0x68** | - | AD0 接地确定地址 |
| | AP3216C | **中断 INT** | PA4 | GPIO A4 | **0x1E** | - | 芯片固定地址 |
| **QSPI Flash** | QSPI CLK/NCS | - | PE10/PE11 | GPIO E10/E11 | N/A | AF10 | 外部 W25Q128 |
| | QSPI IO0-IO3 | - | PE12-PE15 | GPIO E12-E15 | N/A | AF10 | 外部 W25Q128 |
| **ST7789V 屏幕** | **SPI3 SCK** | LCD\_SPI\_SCK | **PB3** | GPIO B3 | N/A | AF6 | SPI 时钟 |
| | **SPI3 MOSI** | LCD\_SPI\_SDA | **PB5** | GPIO B5 | N/A | AF6 | SPI 数据线 |
| | **GPIO Output** | **LCD\_WR** (D/C) | **PB4** | GPIO B4 | N/A | - | 数据/命令切换 (**高电平数据**) |
| | **GPIO Output** | **LCD\_CS** | **PD7** | GPIO D7 | N/A | - | 片选 (**低电平有效**) |
| | **GPIO Output** | **LCD\_RESET** | **PB6** | GPIO B6 | N/A | - | 复位 (**低电平有效**) |
| | **GPIO Output** | **LCD\_PWR** (BL) | **PB7** | GPIO B7 | N/A | - | 背光 (**高电平点亮**) |