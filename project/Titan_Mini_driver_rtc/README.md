# RTC Driver Example

**English** | [**中文**](README_zh.md)

## Introduction

This example demonstrates how to use the RTC (Real-Time Clock) peripheral on the **Titan Board Mini** with **RT-Thread's RTC device driver framework**.

Main features include:
- Initialization of RA8P1 RTC hardware
- Time and date reading/setting
- Alarm functionality
- Battery backup support

## RTC Introduction

### 1. Overview

The **Real-Time Clock (RTC)** is a dedicated hardware module that keeps track of time and date even when the main power is removed.

### 2. RA8P1 RTC Features

- **Calendar function**: Year, month, day, hour, minute, second
- **Alarm support**: Programmable alarm events
- **Battery backup**: Maintains time when main power is off
- **Leap year compensation**: Automatic leap year handling

### 3. RT-Thread RTC Framework

RT-Thread provides RTC support through the device framework:
- `rt_device_find()` - Find RTC device
- `rt_device_open()` - Open RTC device
- `rt_device_read()` - Read time
- `rt_device_write()` - Set time

## Hardware Description

RTC is powered by a dedicated battery pin. Please refer to the hardware manual for details.

## Software Description

The source code is located in `src/hal_entry.c`.

## Compilation & Download

1. Open the project in RT-Thread Studio
2. Build the project
3. Connect the debug probe
4. Download the firmware

## Run Effect

The terminal will display current time and date.

## Further Reading

- [RT-Thread RTC Device](https://www.rt-thread.org/document/site/#/rt-thread-version/rt-thread-standard/programming-manual/device/rtc/rtc)
- [RA8P1 Hardware Manual](./docs/ra8p1-user-manual.pdf)
