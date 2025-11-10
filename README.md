# Zephyr命令

## 代理设置方法
$env:http_proxy="http://127.0.0.1:10808"
$env:https_proxy="http://127.0.0.1:10808"

## 命令输出筛选功能
west boards | Select-String -Pattern "pandora"

## 编译指定开发板
west build -p always -b stm32f429i_disc1 samples\basic\blinky
west build -p auto -b pandora_stm32l475 ..\application

## overlay设置
### PWM功能
#include <dt-bindings/pinctrl/stm32-pinctrl.h> 
#include <dt-bindings/pwm/pwm.h> # Zephyr_App
