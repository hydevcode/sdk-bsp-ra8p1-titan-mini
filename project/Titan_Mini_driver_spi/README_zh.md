# SPI 驱动示例

**中文** | [**English**](README.md)

## 简介

本示例展示如何在 **Titan Board Mini** 上使用 **RT-Thread SPI 设备驱动框架** 来实现 SPI 通信功能。

主要功能包括：
- 初始化 RA8P1 SPI 硬件
- SPI 主模式通信
- DMA 支持高速传输
- 集成各种 SPI 外设

## SPI 简介

### 1. 概述

**SPI（串行外设接口）** 是一种同步串行通信协议，广泛用于连接微控制器与传感器、显示屏和存储设备。

### 2. RA8P1 SPI 特性

- **高速**：最高可达数 MHz
- **多片选**：支持多个从设备
- **DMA 支持**：高效数据传输
- **多种模式**：可配置时钟极性和相位

### 3. RT-Thread SPI 框架

RT-Thread 提供 SPI 设备驱动框架：
- `rt_device_find()` - 查找 SPI 总线
- `rt_spi_configure()` - 配置 SPI 参数
- `rt_spi_transfer()` - 传输数据

## 硬件说明

SPI 引脚连接至各种外设。具体引脚映射请参考硬件手册。

## 软件说明

源码位于 `src/hal_entry.c`。

## 编译下载

1. 在 RT-Thread Studio 中打开工程
2. 编译工程
3. 连接调试器
4. 下载固件

## 运行效果

终端将显示 SPI 通信状态。

## 进阶阅读

- [RT-Thread SPI 设备](https://www.rt-thread.org/document/site/#/rt-thread-version/rt-thread-standard/programming-manual/device/spi/spi)
- [RA8P1 硬件手册](./docs/ra8p1-user-manual.pdf)
