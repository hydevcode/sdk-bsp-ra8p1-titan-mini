# RTC 驱动示例

**中文** | [**English**](README.md)

## 简介

本示例展示如何在 **Titan Board Mini** 上使用 **RT-Thread RTC 设备驱动框架** 来实现实时时钟功能。

主要功能包括：
- 初始化 RA8P1 RTC 硬件
- 读取/设置时间和日期
- 闹钟功能
- 电池备份支持

## RTC 简介

### 1. 概述

**实时时钟（RTC）** 是一个专用硬件模块，即使在主电源移除后也能继续跟踪时间和日期。

### 2. RA8P1 RTC 特性

- **日历功能**：年、月、日、时、分、秒
- **闹钟支持**：可编程闹钟事件
- **电池备份**：主电源关闭时维持时间
- **闰年补偿**：自动闰年处理

### 3. RT-Thread RTC 框架

RT-Thread 通过设备框架提供 RTC 支持：
- `rt_device_find()` - 查找 RTC 设备
- `rt_device_open()` - 打开 RTC 设备
- `rt_device_read()` - 读取时间
- `rt_device_write()` - 设置时间

## 硬件说明

RTC 由专用电池引脚供电。具体请参考硬件手册。

## 软件说明

源码位于 `src/hal_entry.c`。

## 编译下载

1. 在 RT-Thread Studio 中打开工程
2. 编译工程
3. 连接调试器
4. 下载固件

## 运行效果

终端将显示当前时间和日期。

## 进阶阅读

- [RT-Thread RTC 设备](https://www.rt-thread.org/document/site/#/rt-thread-version/rt-thread-standard/programming-manual/device/rtc/rtc)
- [RA8P1 硬件手册](./docs/ra8p1-user-manual.pdf)
