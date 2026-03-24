# ADC 驱动示例

**中文** | [**English**](README.md)

## 简介

本示例展示如何在 **Titan Board Mini** 上使用 **RT-Thread ADC 设备驱动框架** 来实现模数转换功能。

主要功能包括：
- 初始化 RA8P1 ADC 硬件
- 单通道和多通道 ADC 转换
- 读取模拟电压值
- 集成 RT-Thread 设备框架

## ADC 简介

### 1. 概述

**ADC（模数转换器）** 是嵌入式系统中将模拟信号转换为数字信号的关键外设。这对于读取传感器、监测电压等级和处理现实世界的模拟信号至关重要。

### 2. RA8P1 ADC 特性

RA8P1 微控制器具有高性能 ADC 模块：
- **16 位分辨率**，高精度
- **多通道**支持同时采样
- **转换模式**：单次转换、连续转换、扫描模式
- **硬件触发**同步采样
- **DMA 支持**高效数据传输

### 3. RT-Thread ADC 框架

RT-Thread 提供了统一的 ADC 设备驱动框架：

- `rt_device_find()` - 查找 ADC 设备
- `rt_device_open()` - 打开 ADC 设备
- `rt_device_read()` - 读取 ADC 值
- `rt_device_control()` - 配置 ADC 参数

## 硬件说明

ADC 通道连接至板上的各种模拟传感器。具体引脚连接请参考硬件手册。

## 软件说明

源码位于 `src/hal_entry.c`。主要操作包括：

```c
/* ADC 设备名称 */
#define ADC_DEV_NAME "adc1"

/* ADC 通道 */
#define ADC_CHANNEL 0

rt_adc_device_t adc_dev;
rt_uint32_t value;

/* 查找并打开 ADC 设备 */
adc_dev = (rt_adc_device_t)rt_device_find(ADC_DEV_NAME);
rt_adc_enable(adc_dev, ADC_CHANNEL);

/* 读取 ADC 值 */
value = rt_adc_read(adc_dev, ADC_CHANNEL);
```

## 编译下载

1. 在 RT-Thread Studio 中打开工程
2. 点击构建按钮进行编译
3. 连接调试器到开发板
4. 下载固件到开发板

## 运行效果

烧录后，终端将显示：

```
 \ | /
- RT -     Thread Operating System
 / | \     5.1.0 build Jan 01 2025 00:00:00
 2006 - 2024 Copyright by RT-Thread team

ADC 设备示例
ADC 值: 2048
电压: 1.65V
```

## 进阶阅读

- [RT-Thread ADC 设备](https://www.rt-thread.org/document/site/#/rt-thread-version/rt-thread-standard/programming-manual/device/adc/adc)
- [RA8P1 硬件手册](./docs/ra8p1-user-manual.pdf)
