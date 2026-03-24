# GPT 驱动示例

**中文** | [**English**](README.md)

## 简介

本示例展示如何在 **Titan Board Mini** 上使用 **RT-Thread 定时器设备驱动框架** 来实现 GPT（通用定时器）功能。

主要功能包括：
- 初始化 RA8P1 GPT 硬件
- PWM 输出生成
- 输入捕获功能
- 定时器中断处理

## GPT 简介

### 1. 概述

**通用定时器（GPT）** 是一个多功能的定时器模块，能够执行各种定时和计数功能，包括 PWM 生成和输入捕获。

### 2. RA8P1 GPT 特性

- **高分辨率**：最高 32 位计数器
- **多通道**：支持多个 PWM 输出
- **PWM 模式**：多种 PWM 生成模式
- **输入捕获**：用于测量脉冲宽度

### 3. RT-Thread 定时器框架

RT-Thread 通过设备框架提供硬件定时器支持。

## 硬件说明

GPT 引脚连接至各种外设。具体引脚映射请参考硬件手册。

## 软件说明

源码位于 `src/hal_entry.c`。

## 编译下载

1. 在 RT-Thread Studio 中打开工程
2. 编译工程
3. 连接调试器
4. 下载固件

## 运行效果

终端将显示 GPT/PWM 操作状态。

## 进阶阅读

- [RT-Thread PWM 设备](https://www.rt-thread.org/document/site/#/rt-thread-version/rt-thread-standard/programming-manual/device/pwm/pwm)
- [RA8P1 硬件手册](./docs/ra8p1-user-manual.pdf)
