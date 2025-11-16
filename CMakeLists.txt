# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(application)

# 1. 添加应用程序私有的头文件目录
# PRIVATE 表示这个 include 路径只对当前 app target 有效
target_include_directories(app PRIVATE
    include
    drivers/include
)

# 2. 添加所有源文件到应用程序目标
target_sources(app PRIVATE
    src/main.c
    src/led_thread.c
    src/ap3216c_thread.c
    drivers/ap3216c.c
)

# 3. (可选) 链接所需的 Zephyr 库
# 正确的链接方式：只需要链接到 'zephyr' 目标
target_link_libraries(app PRIVATE
    zephyr
)