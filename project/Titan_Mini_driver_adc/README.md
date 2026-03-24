# ADC Driver Example

**English** | [**中文**](README_zh.md)

## Introduction

This example demonstrates how to use the ADC (Analog-to-Digital Converter) peripheral on the **Titan Board Mini** with **RT-Thread's ADC device driver framework**.

Main features include:
- Initialization of RA8P1 ADC hardware
- Single-channel and multi-channel ADC conversion
- Reading analog voltage values
- Integration with RT-Thread's device framework

## ADC Introduction

### 1. Overview

**ADC (Analog-to-Digital Converter)** is a crucial peripheral that converts analog signals into digital values. This is essential for reading sensors, monitoring voltage levels, and processing real-world analog signals in embedded systems.

### 2. RA8P1 ADC Features

The RA8P1 microcontroller features high-performance ADC modules:
- **16-bit resolution** for high precision
- **Multiple channels** supporting simultaneous sampling
- **Conversion modes**: Single conversion, continuous conversion, scan mode
- **Hardware triggers** for synchronized sampling
- **DMA support** for efficient data transfer

### 3. RT-Thread ADC Framework

RT-Thread provides a unified ADC device driver framework:

- `rt_device_find()` - Find ADC device
- `rt_device_open()` - Open ADC device
- `rt_device_read()` - Read ADC value
- `rt_device_control()` - Configure ADC parameters

## Hardware Description

The ADC channel is connected to various analog sensors on the board. Please refer to the hardware manual for pin connections.

## Software Description

The source code is located in `src/hal_entry.c`. Key operations include:

```c
/* ADC device name */
#define ADC_DEV_NAME "adc1"

/* ADC channel */
#define ADC_CHANNEL 0

rt_adc_device_t adc_dev;
rt_uint32_t value;

/* Find and open ADC device */
adc_dev = (rt_adc_device_t)rt_device_find(ADC_DEV_NAME);
rt_adc_enable(adc_dev, ADC_CHANNEL);

/* Read ADC value */
value = rt_adc_read(adc_dev, ADC_CHANNEL);
```

## Compilation & Download

1. Open the project in RT-Thread Studio
2. Build the project using the build button
3. Connect the debug probe to the development board
4. Download the firmware to the board

## Run Effect

After programming, the terminal will display:

```
 \ | /
- RT -     Thread Operating System
 / | \     5.1.0 build Jan 01 2025 00:00:00
 2006 - 2024 Copyright by RT-Thread team

ADC device demo
ADC value: 2048
Voltage: 1.65V
```

## Further Reading

- [RT-Thread ADC Device](https://www.rt-thread.org/document/site/#/rt-thread-version/rt-thread-standard/programming-manual/device/adc/adc)
- [RA8P1 Hardware Manual](./docs/ra8p1-user-manual.pdf)
