# SPI Driver Example

**English** | [**中文**](README_zh.md)

## Introduction

This example demonstrates how to use the SPI (Serial Peripheral Interface) peripheral on the **Titan Board Mini** with **RT-Thread's SPI device driver framework**.

Main features include:
- Initialization of RA8P1 SPI hardware
- SPI master mode communication
- DMA support for high-speed transfer
- Integration with various SPI peripherals

## SPI Introduction

### 1. Overview

**SPI (Serial Peripheral Interface)** is a synchronous serial communication protocol widely used for connecting microcontrollers to sensors, displays, and storage devices.

### 2. RA8P1 SPI Features

- **High speed**: Up to several MHz
- **Multiple chip selects**: Support for multiple slaves
- **DMA support**: Efficient data transfer
- **Various modes**: Configurable clock polarity and phase

### 3. RT-Thread SPI Framework

RT-Thread provides SPI device driver framework:
- `rt_device_find()` - Find SPI bus
- `rt_spi_configure()` - Configure SPI parameters
- `rt_spi_transfer()` - Transfer data

## Hardware Description

SPI pins are connected to various peripherals. Please refer to the hardware manual for pin mappings.

## Software Description

The source code is located in `src/hal_entry.c`.

## Compilation & Download

1. Open the project in RT-Thread Studio
2. Build the project
3. Connect the debug probe
4. Download the firmware

## Run Effect

The terminal will display SPI communication status.

## Further Reading

- [RT-Thread SPI Device](https://www.rt-thread.org/document/site/#/rt-thread-version/rt-thread-standard/programming-manual/device/spi/spi)
- [RA8P1 Hardware Manual](./docs/ra8p1-user-manual.pdf)
