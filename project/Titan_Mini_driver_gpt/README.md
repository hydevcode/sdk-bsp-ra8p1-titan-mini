# GPT Driver Example

**English** | [**中文**](README_zh.md)

## Introduction

This example demonstrates how to use the GPT (General Purpose Timer) peripheral on the **Titan Board Mini** with **RT-Thread's timer device driver framework**.

Main features include:
- Initialization of RA8P1 GPT hardware
- PWM output generation
- Input capture functionality
- Timer interrupt handling

## GPT Introduction

### 1. Overview

The **General Purpose Timer (GPT)** is a versatile timer module capable of various timing and counting functions, including PWM generation and input capture.

### 2. RA8P1 GPT Features

- **High resolution**: Up to 32-bit counter
- **Multiple channels**: Support for multiple PWM outputs
- **PWM modes**: Various PWM generation modes
- **Input capture**: For measuring pulse widths

### 3. RT-Thread Timer Framework

RT-Thread provides hardware timer support through the device framework.

## Hardware Description

GPT pins are connected to various peripherals. Please refer to the hardware manual for pin mappings.

## Software Description

The source code is located in `src/hal_entry.c`.

## Compilation & Download

1. Open the project in RT-Thread Studio
2. Build the project
3. Connect the debug probe
4. Download the firmware

## Run Effect

The terminal will display GPT/PWM operation status.

## Further Reading

- [RT-Thread PWM Device](https://www.rt-thread.org/document/site/#/rt-thread-version/rt-thread-standard/programming-manual/device/pwm/pwm)
- [RA8P1 Hardware Manual](./docs/ra8p1-user-manual.pdf)
