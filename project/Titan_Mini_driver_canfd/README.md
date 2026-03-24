# CAN FD Driver Example

**English** | [**中文**](README_zh.md)

## Introduction

This example demonstrates how to use the CAN FD (Controller Area Network with Flexible Data-Rate) peripheral on the **Titan Board Mini** with **RT-Thread's CAN device driver framework**.

Main features include:
- Initialization of RA8P1 CAN FD hardware
- CAN FD frame transmission and reception
- Support for standard and extended identifiers
- Integration with RT-Thread's device framework

## CAN FD Introduction

### 1. Overview

**CAN FD (CAN with Flexible Data-Rate)** is an improved version of the classic CAN bus, offering higher data rates and larger payload sizes while maintaining backward compatibility with standard CAN.

### 2. CAN FD Features

- **Higher data rate**: Up to 8 Mbps in the data phase
- **Larger payload**: Up to 64 bytes per frame (vs 8 bytes for classic CAN)
- **Improved error handling**: Better error detection and confinement
- **Backward compatible**: Works with standard CAN 2.0 devices

### 3. RT-Thread CAN Framework

RT-Thread provides a unified CAN device driver framework:

- `rt_device_find()` - Find CAN device
- `rt_device_open()` - Open CAN device
- `rt_device_write()` - Send CAN frame
- `rt_device_read()` - Receive CAN frame

## Hardware Description

The CAN FD transceiver is connected to the MCU's CAN pins. Please refer to the hardware manual for pin connections.

## Software Description

The source code is located in `src/hal_entry.c`.

## Compilation & Download

1. Open the project in RT-Thread Studio
2. Build the project
3. Connect the debug probe
4. Download the firmware

## Run Effect

The terminal will display CAN FD communication status.

## Further Reading

- [RT-Thread CAN Device](https://www.rt-thread.org/document/site/#/rt-thread-version/rt-thread-standard/programming-manual/device/can/can)
- [RA8P1 Hardware Manual](./docs/ra8p1-user-manual.pdf)
