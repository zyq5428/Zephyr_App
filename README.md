# Zephyr命令

## 代理设置方法
```
$env:http_proxy="http://127.0.0.1:10808"
$env:https_proxy="http://127.0.0.1:10808"
```

## 命令输出筛选功能
```
west boards | Select-String -Pattern "pandora"
```

## 查找当前目录及其所有子目录中，文件名包含 'ap3216c' 的所有文件
```
Get-ChildItem -Recurse -Filter '*ap3216c*' -ErrorAction SilentlyContinue
```

## 查找 dts/bindings/sensor 目录下所有 *.yaml 文件内容中包含 'ap3216c' 的文件
```
Get-ChildItem -Path .\dts\bindings\sensor -Recurse -Include *.yaml | Select-String -Pattern "ap3216c" -AllMatches
```
- 命令详解：Get-ChildItem -Path .\dts\bindings\sensor -Recurse -Include *.yaml:
    - 首先，找到 dts\bindings\sensor 目录下所有的 YAML 文件。
    - '|': 将上一个命令的输出（文件对象）作为输入，传递给下一个命令。
    - Select-String -Pattern "ap3216c" -AllMatches:
        - 在传入的文件对象的内容中搜索 "ap3216c" 字符串。
        - 输出结果会显示文件名和匹配的行号。

## 编译指定开发板
```
west build -p always -b stm32f429i_disc1 samples\basic\blinky
west build -p auto -b pandora_stm32l475 ..\application
west build -b pandora_stm32l475 -t menuconfig
```

## overlay设置

### PWM功能
```
#include <dt-bindings/pinctrl/stm32-pinctrl.h>
#include <dt-bindings/pwm/pwm.h>
```
