#ifndef OV5640_TUNING_H
#define OV5640_TUNING_H

#include <rtthread.h>

rt_err_t ov5640_tuning_apply_low_cip(void);
rt_err_t ov5640_tuning_apply_ev(int ev);
rt_err_t ov5640_tuning_set_ae_ag(rt_bool_t enable);
rt_err_t ov5640_tuning_get_shutter(rt_uint32_t *shutter);
rt_err_t ov5640_tuning_set_shutter(rt_uint32_t shutter);
rt_err_t ov5640_tuning_get_gain16(rt_uint16_t *gain16);
rt_err_t ov5640_tuning_set_gain16(rt_uint16_t gain16);
rt_err_t ov5640_tuning_set_ae_target(rt_uint8_t target);
rt_err_t ov5640_tuning_apply_banding(int light_freq);
rt_err_t ov5640_tuning_apply_sensor_mode(const char *name);
rt_err_t ov5640_tuning_apply_sensor_mode_stopped(const char *name, rt_uint16_t *width, rt_uint16_t *height);
void ov5640_focus_service_start(void);
void ov5640_focus_trigger(void);

#endif
