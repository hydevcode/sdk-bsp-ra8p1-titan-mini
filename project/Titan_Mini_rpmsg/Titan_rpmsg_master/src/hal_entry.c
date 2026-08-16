/*
 * Copyright (c) 2006-2024, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include "hal_data.h"
#include "camera_layer.h"
#include "model_select.h"
#if TITAN_USING_FACE_MODEL
#include "face_detect_tcp.h"
#endif
#if TITAN_USING_HANDWRITING_MODEL
#include "handwriting_model.h"
#endif
#include "ov5640_tuning.h"

#define CAMERA_USE_SENSOR_TEST_PATTERN  (0)
#define CAMERA_MCLK_PWM_DEVICE          "pwm12"
#define CAMERA_MCLK_PWM_CHANNEL         (0)
#define CAMERA_MCLK_PWM_PERIOD          (1000)
#define CAMERA_MCLK_PWM_PULSE           (1000)

extern void camera_tcp_frame_publish(uint8_t const *src, rt_uint32_t sequence);
extern void camera_tcp_auto_start(void);
extern rt_uint16_t camera_tcp_rotation_get(void);

#if TITAN_USING_FACE_MODEL
static rt_bool_t camera_face_enable = RT_TRUE;
static rt_uint32_t camera_face_interval = 1U;
static rt_uint32_t camera_face_seen;
#endif

void DisplayVsyncCallback(display_callback_args_t *p_args)
{
    RT_UNUSED(p_args);
}

static rt_err_t camera_mclk_start(void)
{
    struct rt_device_pwm *pwm_dev = (struct rt_device_pwm *)rt_device_find(CAMERA_MCLK_PWM_DEVICE);

    if (pwm_dev == RT_NULL)
    {
        return -RT_ERROR;
    }

    rt_pwm_enable(pwm_dev, CAMERA_MCLK_PWM_CHANNEL);
    rt_pwm_set(pwm_dev, CAMERA_MCLK_PWM_CHANNEL, CAMERA_MCLK_PWM_PERIOD, CAMERA_MCLK_PWM_PULSE);
    return RT_EOK;
}

void hal_entry(void)
{
    fsp_err_t fsp_status;

    if (RT_EOK != camera_mclk_start())
    {
        return;
    }

    fsp_status = camera_init(CAMERA_USE_SENSOR_TEST_PATTERN != 0);
    if (FSP_SUCCESS != fsp_status)
    {
        return;
    }
    (void)ov5640_tuning_apply_low_cip();

    camera_image_buffer_initialize();

    fsp_status = camera_capture_start();
    if (FSP_SUCCESS != fsp_status)
    {
        return;
    }
    ov5640_focus_service_start();

#if TITAN_USING_FACE_MODEL
    if ((camera_face_enable == RT_TRUE) && (RT_EOK != face_detect_tcp_init()))
    {
        camera_face_enable = RT_FALSE;
    }
#elif TITAN_USING_HANDWRITING_MODEL
    (void)handwriting_model_init();
#endif

    camera_tcp_auto_start();

    while (1)
    {
        if (camera_capture_post_process() == 0U)
        {
            rt_thread_mdelay(1);
            continue;
        }

        uint8_t const *frame = RT_NULL;
        rt_uint32_t sequence = 0;

        if (camera_processed_frame_get(&frame, &sequence) != 0U)
        {
#if TITAN_USING_HANDWRITING_MODEL
            (void)handwriting_model_process_rgb565((uint16_t *)frame,
                                                   camera_capture_output_width_get(),
                                                   camera_capture_output_height_get(),
                                                   camera_tcp_rotation_get());
#endif
            camera_tcp_frame_publish(frame, sequence);

#if TITAN_USING_FACE_MODEL
            if (camera_face_enable == RT_TRUE)
            {
                camera_face_seen++;
                if ((camera_face_interval > 0U) && ((camera_face_seen % camera_face_interval) == 0U))
                {
                    (void)face_detect_tcp_process_rgb565((uint16_t *)frame,
                                                         camera_capture_output_width_get(),
                                                         camera_capture_output_height_get());
                }
            }
#endif
        }
    }
}
