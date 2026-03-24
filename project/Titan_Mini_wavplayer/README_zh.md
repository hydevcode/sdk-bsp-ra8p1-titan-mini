# WAV 音频播放示例

**中文** | [**English**](README.md)

## 简介

本示例展示如何在 **Titan Board Mini** 上使用 RT-Thread 音频框架播放 WAV 音频文件。

主要功能包括：
- SD 卡文件系统集成
- WAV 音频文件解码
- I2S 音频输出
- 音频播放控制

## 音频简介

### 1. 概述

WAV 是一种音频文件格式，以未压缩的 PCM 格式存储音频数据。它能提供高质量的音频播放。

### 2. 音频系统

- **I2S 接口**：用于数字音频传输
- **音频编解码器**：用于 DAC 转换
- **文件系统**：用于从 SD 卡读取音频文件

### 3. RT-Thread 音频框架

RT-Thread 提供音频设备框架：
- `rt_device_find()` - 查找音频设备
- `rt_device_open()` - 打开音频设备
- `rt_device_write()` - 播放音频数据

## 硬件说明

音频输出通过 I2S 接口连接到音频编解码器。具体引脚连接请参考硬件手册。

## 软件说明

源码位于 `src/hal_entry.c`。

## 编译下载

1. 在 RT-Thread Studio 中打开工程
2. 编译工程
3. 连接调试器
4. 下载固件

## 运行效果

终端将显示音频播放状态和控制信息。

## 进阶阅读

- [RT-Thread 音频](https://www.rt-thread.org/document/site/)
- [RA8P1 硬件手册](./docs/ra8p1-user-manual.pdf)
