/*
* Copyright (c) 2020 - 2025 Renesas Electronics Corporation and/or its affiliates
*
* SPDX-License-Identifier: BSD-3-Clause
*/
/**********************************************************************************************************************
 * File Name    : camera_layer.h
 * Version      : .
 * Description  : .
 *********************************************************************************************************************/

#ifndef CAMERA_CAMERA_LAYER_H_
 #define CAMERA_CAMERA_LAYER_H_

#include <rtthread.h>
#include <rtdevice.h>
#include "hal_data.h"
#include "arducam.h"
#include "camera_layer_config.h"

#define CONFIG_BRIGHTNESS_DISABLE (0)
#define CONFIG_BRIGHTNESS_MINUS_3 (1)
#define CONFIG_BRIGHTNESS_MINUS_2 (2)
#define CONFIG_BRIGHTNESS_MINUS_1 (3)
#define CONFIG_BRIGHTNESS_ZERO    (4)
#define CONFIG_BRIGHTNESS_PLUS_1  (5)
#define CONFIG_BRIGHTNESS_PLUS_2  (6)
#define CONFIG_BRIGHTNESS_PLUS_3  (7)

#define CONFIG_CONTRAST_DISBALE (0)
#define CONFIG_CONTRAST_MINUS_3 (1)
#define CONFIG_CONTRAST_MINUS_2 (2)
#define CONFIG_CONTRAST_MINUS_1 (3)
#define CONFIG_CONTRAST_ZERO    (4)
#define CONFIG_CONTRAST_PLUS_1  (5)
#define CONFIG_CONTRAST_PLUS_2  (6)
#define CONFIG_CONTRAST_PLUS_3  (7)

typedef struct ov_reg
{
    uint16_t      reg_num;
    unsigned char value;
} st_ov_reg_t;

typedef enum
{
    BSP_CAM_YUV422  = 0,
    BSP_CAM_RAW_RGB = 1,
} bsp_camera_output_t;

typedef enum
{
    POWER_UP   = BSP_IO_LEVEL_LOW,
    POWER_DOWN = BSP_IO_LEVEL_HIGH,
} bsp_camera_power_t;

typedef enum
{
    BSP_CAM_FRAMERATE_10FPS  = 100,
    BSP_CAM_FRAMERATE_20FPS  = 50,
    BSP_CAM_FRAMERATE_33FPS  = 30,
    BSP_CAM_FRAMERATE_40FPS  = 25,
    BSP_CAM_FRAMERATE_50FPS  = 20,
    BSP_CAM_FRAMERATE_100FPS = 10,
} bsp_camera_framerate_t;

typedef enum
{
    BSP_CAM_VGA   = 0,                 // 640x480
    BSP_CAM_CIF   = 1,                 // 352x288
    BSP_CAM_QVGA  = 2,                 // 320x240
    BSP_CAM_QCIF  = 3,                 // 176x144
    BSP_CAM_QQVGA = 4,                 // 160x120
} bsp_camera_resolution_t;

typedef enum
{
    BSP_CAM_VGA_WIDTH    = 640,
    BSP_CAM_VGA_HEIGHT   = 480,
    BSP_CAM_CIF_WIDTH    = 352,
    BSP_CAM_CIF_HEIGHT   = 288,
    BSP_CAM_QVGA_WIDTH   = 320,
    BSP_CAM_QVGA_HEIGHT  = 240,
    BSP_CAM_QCIF_WIDTH   = 176,
    BSP_CAM_QCIF_HEIGHT  = 144,
    BSP_CAM_QQVGA_WIDTH  = 160,
    BSP_CAM_QQVGA_HEIGHT = 120,
} bsp_camera_size_list_t;


extern uint8_t  camera_capture_image_rgb565[CAMERA_OV5640_RUNTIME_MAX_WIDTH * CAMERA_OV5640_RUNTIME_MAX_HEIGHT * CAMERA_IMAGE_BYTE_PER_PIXEL];
extern uint32_t camera_capture_image_rgb565_size;

#define CAM_RST_PIN         (BSP_IO_PORT_07_PIN_08)

FSP_CPP_HEADER
fsp_err_t camera_init (bool use_test_mode);
void      camera_user_callback_set(void (* p_callback)(void *));
void      camera_image_buffer_initialize(void);

fsp_err_t camera_capture_start(void);
void      camera_capture_stream_pause(void);
void      camera_capture_stream_resume(void);
void      camera_capture_stream_pause_vin(void);
void      camera_capture_stream_resume_vin(void);
fsp_err_t camera_capture_scale_set(uint16_t width, uint16_t height);
uint16_t  camera_capture_output_width_get(void);
uint16_t  camera_capture_output_height_get(void);
uint32_t  camera_capture_output_bytes_get(void);
uint32_t  camera_capture_buffer_capacity_get(void);
uint32_t  camera_data_ready_buffer_pointer_get(void);
uint32_t  camera_frame_sequence_get(void);
void      camera_frame_cache_discard(uint8_t const * p_buffer, uint32_t length);

uint32_t      camera_capture_post_process(void);
uint32_t      camera_processed_frame_get(uint8_t const ** pp_buffer, uint32_t * p_sequence);

int OV5640_af_init(void);
int OV5640_auto_focus(void);
int OV5640_release_focus(void);

FSP_CPP_FOOTER

#endif /* CAMERA_CAMERA_LAYER_H_ */
