# IMU 传感器示例

**中文** | [**English**](README.md)

## 简介

本示例展示如何在 **Titan Board Mini** 上使用 **RT-Thread 传感器框架** 来实现 IMU（惯性测量单元）传感器功能。

主要功能包括：
- 初始化 IMU 传感器（加速度计、陀螺仪）
- 读取 6 轴运动数据
- 集成 RT-Thread 传感器框架

## IMU 简介

### 1. 概述

**IMU（惯性测量单元）** 通常由加速度计和陀螺仪组成，用于测量加速度和角速度。

### 2. 常见 IMU 传感器

- **MPU6050**：6 轴 IMU（3 轴陀螺仪 + 3 轴加速度计）
- **BMI160**：高性能 6 轴 IMU
- **ICM42688**：高精度 6 轴 IMU

### 3. RT-Thread 传感器框架

RT-Thread 提供传感器设备框架：
- `rt_device_find()` - 查找传感器设备
- `rt_device_open()` - 打开传感器设备
- `rt_device_read()` - 读取传感器数据

## 硬件说明

IMU 传感器通过 I2C 或 SPI 连接。具体引脚连接请参考硬件手册。

## 软件说明

源码位于 `src/hal_entry.c`。

## 编译下载

1. 在 RT-Thread Studio 中打开工程
2. 编译工程
3. 连接调试器
4. 下载固件

## 运行效果

终端将显示 IMU 传感器数据（加速度和陀螺仪值）。

## 进阶阅读

- [RT-Thread 传感器](https://www.rt-thread.org/document/site/)
- [RA8P1 硬件手册](./docs/ra8p1-user-manual.pdf)
