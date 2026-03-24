# CAN FD 驱动示例

**中文** | [**English**](README.md)

## 简介

本示例展示如何在 **Titan Board Mini** 上使用 **RT-Thread CAN 设备驱动框架** 来实现 CAN FD 通信功能。

主要功能包括：
- 初始化 RA8P1 CAN FD 硬件
- CAN FD 帧发送和接收
- 支持标准帧和扩展帧
- 集成 RT-Thread 设备框架

## CAN FD 简介

### 1. 概述

**CAN FD（可变速率的 CAN）** 是经典 CAN 总线的改进版本，在保持与标准 CAN 向后兼容的同时，提供更高的数据速率和更大的数据负载。

### 2. CAN FD 特性

- **更高的数据速率**：数据阶段最高 8 Mbps
- **更大的数据负载**：每帧最高 64 字节（经典 CAN 为 8 字节）
- **改进的错误处理**：更好的错误检测和限制
- **向后兼容**：可与标准 CAN 2.0 设备配合工作

### 3. RT-Thread CAN 框架

RT-Thread 提供了统一的 CAN 设备驱动框架：

- `rt_device_find()` - 查找 CAN 设备
- `rt_device_open()` - 打开 CAN 设备
- `rt_device_write()` - 发送 CAN 帧
- `rt_device_read()` - 接收 CAN 帧

## 硬件说明

CAN FD 收发器连接至 MCU 的 CAN 引脚。具体引脚连接请参考硬件手册。

## 软件说明

源码位于 `src/hal_entry.c`。

## 编译下载

1. 在 RT-Thread Studio 中打开工程
2. 编译工程
3. 连接调试器
4. 下载固件

## 运行效果

终端将显示 CAN FD 通信状态。

## 进阶阅读

- [RT-Thread CAN 设备](https://www.rt-thread.org/document/site/#/rt-thread-version/rt-thread-standard/programming-manual/device/can/can)
- [RA8P1 硬件手册](./docs/ra8p1-user-manual.pdf)
