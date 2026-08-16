#include "ov5640_tuning.h"

#include "camera_layer.h"

typedef struct ov5640_reg8
{
    rt_uint16_t reg;
    rt_uint8_t value;
} ov5640_reg8_t;

#define OV5640_XCLK_HZ              (24000000U)
#define OV5640_GAIN16_MIN           (16U)
#define OV5640_GAIN16_MAX           (1023U)
#define OV5640_SHUTTER_MAX          (0xFFFBU)
#define OV5640_AE_TARGET_DEFAULT    (52U)

static rt_thread_t ov5640_focus_thread = RT_NULL;
static rt_sem_t ov5640_focus_sem = RT_NULL;
static rt_bool_t ov5640_focus_ready = RT_FALSE;

static const ov5640_reg8_t ov5640_cip_low_regs[] =
{
    {0x5300, 0x08}, {0x5301, 0x30}, {0x5302, 0x10}, {0x5303, 0x00},
    {0x5304, 0x08}, {0x5305, 0x30}, {0x5306, 0x08}, {0x5307, 0x16},
    {0x5309, 0x08}, {0x530a, 0x30}, {0x530b, 0x04}, {0x530c, 0x06},
};

static const ov5640_reg8_t ov5640_ev_minus_2_regs[] =
{
    {0x3a0f, 0x28}, {0x3a10, 0x20}, {0x3a1b, 0x28}, {0x3a1e, 0x20},
    {0x3a11, 0x50}, {0x3a1f, 0x18}, {0x3a18, 0x00}, {0x3a19, 0x80},
};

static const ov5640_reg8_t ov5640_ev_minus_1_regs[] =
{
    {0x3a0f, 0x34}, {0x3a10, 0x28}, {0x3a1b, 0x34}, {0x3a1e, 0x28},
    {0x3a11, 0x60}, {0x3a1f, 0x1c}, {0x3a18, 0x00}, {0x3a19, 0x80},
};

static const ov5640_reg8_t ov5640_ev_0_regs[] =
{
    {0x3a0f, 0x40}, {0x3a10, 0x30}, {0x3a1b, 0x40}, {0x3a1e, 0x30},
    {0x3a11, 0x71}, {0x3a1f, 0x20}, {0x3a18, 0x00}, {0x3a19, 0x80},
};

static const ov5640_reg8_t ov5640_ev_plus_1_regs[] =
{
    {0x3a0f, 0x50}, {0x3a10, 0x40}, {0x3a1b, 0x50}, {0x3a1e, 0x40},
    {0x3a11, 0x80}, {0x3a1f, 0x28}, {0x3a18, 0x00}, {0x3a19, 0xa0},
};

static const ov5640_reg8_t ov5640_ev_plus_2_regs[] =
{
    {0x3a0f, 0x60}, {0x3a10, 0x50}, {0x3a1b, 0x60}, {0x3a1e, 0x50},
    {0x3a11, 0x90}, {0x3a1f, 0x30}, {0x3a18, 0x00}, {0x3a19, 0xc0},
};

static const ov5640_reg8_t ov5640_ev_plus_3_regs[] =
{
    {0x3a0f, 0x70}, {0x3a10, 0x60}, {0x3a1b, 0x70}, {0x3a1e, 0x60},
    {0x3a11, 0xa0}, {0x3a1f, 0x40}, {0x3a18, 0x00}, {0x3a19, 0xf8},
};

static const ov5640_reg8_t ov5640_mode_vga_30fps_regs[] =
{
    {0x3008, 0x42}, {0x3034, 0x18}, {0x3035, 0x21}, {0x3036, 0x54},
    {0x3037, 0x13}, {0x3108, 0x01}, {0x3c07, 0x08},
    {0x3c09, 0x1c}, {0x3c0a, 0x9c}, {0x3c0b, 0x40},
    {0x3820, 0x41}, {0x3821, 0x07}, {0x3814, 0x31}, {0x3815, 0x31},
    {0x3800, 0x00}, {0x3801, 0x00}, {0x3802, 0x00}, {0x3803, 0x04},
    {0x3804, 0x0a}, {0x3805, 0x3f}, {0x3806, 0x07}, {0x3807, 0x9b},
    {0x3808, 0x02}, {0x3809, 0x80}, {0x380a, 0x01}, {0x380b, 0xe0},
    {0x380c, 0x07}, {0x380d, 0x68}, {0x380e, 0x04}, {0x380f, 0x38},
    {0x3810, 0x00}, {0x3811, 0x10}, {0x3812, 0x00}, {0x3813, 0x06},
    {0x3618, 0x00}, {0x3612, 0x29}, {0x3708, 0x64}, {0x3709, 0x52},
    {0x370c, 0x03}, {0x3a02, 0x03}, {0x3a03, 0xd8}, {0x3a08, 0x01},
    {0x3a09, 0x0e}, {0x3a0a, 0x00}, {0x3a0b, 0xf6}, {0x3a0e, 0x03},
    {0x3a0d, 0x04}, {0x3a14, 0x03}, {0x3a15, 0xd8},
    {0x4001, 0x02}, {0x4004, 0x02}, {0x4005, 0x1a}, {0x4300, 0x3f},
    {0x501f, 0x00}, {0x4713, 0x03}, {0x4407, 0x04},
    {0x4602, 0x02}, {0x4603, 0x80}, {0x4604, 0x01}, {0x4605, 0xe0},
    {0x460b, 0x37}, {0x460c, 0x20}, {0x300e, 0x4c}, {0x4800, 0x20},
    {0x3007, 0xfb}, {0x4837, 0x0a}, {0x3824, 0x04}, {0x5001, 0xa3},
    {0x3008, 0x02}, {0x3503, 0x00},
};

static const ov5640_reg8_t ov5640_mode_qvga_30fps_regs[] =
{
    {0x3008, 0x42}, {0x3034, 0x18}, {0x3035, 0x21}, {0x3036, 0x54},
    {0x3037, 0x13}, {0x3108, 0x01}, {0x3c07, 0x08},
    {0x3c09, 0x1c}, {0x3c0a, 0x9c}, {0x3c0b, 0x40},
    {0x3820, 0x41}, {0x3821, 0x07}, {0x3814, 0x31}, {0x3815, 0x31},
    {0x3800, 0x00}, {0x3801, 0x00}, {0x3802, 0x00}, {0x3803, 0x04},
    {0x3804, 0x0a}, {0x3805, 0x3f}, {0x3806, 0x07}, {0x3807, 0x9b},
    {0x3808, 0x01}, {0x3809, 0x40}, {0x380a, 0x00}, {0x380b, 0xf0},
    {0x380c, 0x07}, {0x380d, 0x68}, {0x380e, 0x03}, {0x380f, 0xd8},
    {0x3810, 0x00}, {0x3811, 0x10}, {0x3812, 0x00}, {0x3813, 0x06},
    {0x3618, 0x00}, {0x3612, 0x29}, {0x3708, 0x64}, {0x3709, 0x52},
    {0x370c, 0x03}, {0x3a02, 0x03}, {0x3a03, 0xd8}, {0x3a08, 0x01},
    {0x3a09, 0x27}, {0x3a0a, 0x00}, {0x3a0b, 0xf6}, {0x3a0e, 0x03},
    {0x3a0d, 0x04}, {0x3a14, 0x03}, {0x3a15, 0xd8},
    {0x4001, 0x02}, {0x4004, 0x02}, {0x4005, 0x1a}, {0x4300, 0x3f},
    {0x501f, 0x00}, {0x4713, 0x03}, {0x4407, 0x04},
    {0x4602, 0x01}, {0x4603, 0x40}, {0x4604, 0x00}, {0x4605, 0xf0},
    {0x460b, 0x37}, {0x460c, 0x20}, {0x300e, 0x4c}, {0x4800, 0x20},
    {0x3007, 0xfb}, {0x4837, 0x0a}, {0x3824, 0x04}, {0x5001, 0xa3},
    {0x3008, 0x02}, {0x3503, 0x00},
};

static const ov5640_reg8_t ov5640_mode_qcif_30fps_regs[] =
{
    {0x3008, 0x42}, {0x3034, 0x18}, {0x3035, 0x21}, {0x3036, 0x54},
    {0x3037, 0x13}, {0x3108, 0x01}, {0x3c07, 0x08},
    {0x3c09, 0x1c}, {0x3c0a, 0x9c}, {0x3c0b, 0x40},
    {0x3820, 0x41}, {0x3821, 0x07}, {0x3814, 0x31}, {0x3815, 0x31},
    {0x3800, 0x00}, {0x3801, 0x00}, {0x3802, 0x00}, {0x3803, 0x04},
    {0x3804, 0x0a}, {0x3805, 0x3f}, {0x3806, 0x07}, {0x3807, 0x9b},
    {0x3808, 0x00}, {0x3809, 0xb0}, {0x380a, 0x00}, {0x380b, 0x90},
    {0x380c, 0x07}, {0x380d, 0x68}, {0x380e, 0x03}, {0x380f, 0xd8},
    {0x3810, 0x00}, {0x3811, 0x10}, {0x3812, 0x00}, {0x3813, 0x06},
    {0x3618, 0x00}, {0x3612, 0x29}, {0x3708, 0x64}, {0x3709, 0x52},
    {0x370c, 0x03}, {0x3a02, 0x03}, {0x3a03, 0xd8}, {0x3a08, 0x01},
    {0x3a09, 0x27}, {0x3a0a, 0x00}, {0x3a0b, 0xf6}, {0x3a0e, 0x03},
    {0x3a0d, 0x04}, {0x3a14, 0x03}, {0x3a15, 0xd8},
    {0x4001, 0x02}, {0x4004, 0x02}, {0x4005, 0x1a}, {0x4300, 0x3f},
    {0x501f, 0x00}, {0x4713, 0x03}, {0x4407, 0x04},
    {0x4602, 0x00}, {0x4603, 0xb0}, {0x4604, 0x00}, {0x4605, 0x90},
    {0x460b, 0x37}, {0x460c, 0x20}, {0x300e, 0x4c}, {0x4800, 0x20},
    {0x3007, 0xfb}, {0x4837, 0x0a}, {0x3824, 0x04}, {0x5001, 0xa3},
    {0x3008, 0x02}, {0x3503, 0x00},
};

static const ov5640_reg8_t ov5640_mode_720p_30fps_regs[] =
{
    {0x3008, 0x42}, {0x3035, 0x21}, {0x3036, 0x54}, {0x3c07, 0x07},
    {0x3c09, 0x1c}, {0x3c0a, 0x9c}, {0x3c0b, 0x40},
    {0x3820, 0x41}, {0x3821, 0x07}, {0x3814, 0x31}, {0x3815, 0x31},
    {0x3800, 0x00}, {0x3801, 0x00}, {0x3802, 0x00}, {0x3803, 0xfa},
    {0x3804, 0x0a}, {0x3805, 0x3f}, {0x3806, 0x06}, {0x3807, 0xa9},
    {0x3808, 0x05}, {0x3809, 0x00}, {0x380a, 0x02}, {0x380b, 0xd0},
    {0x380c, 0x07}, {0x380d, 0x64}, {0x380e, 0x02}, {0x380f, 0xe4},
    {0x3810, 0x00}, {0x3811, 0x10}, {0x3812, 0x00}, {0x3813, 0x04},
    {0x3618, 0x00}, {0x3612, 0x29}, {0x3708, 0x64}, {0x3709, 0x52},
    {0x370c, 0x03}, {0x3a02, 0x02}, {0x3a03, 0xe4}, {0x3a08, 0x01},
    {0x3a09, 0xbc}, {0x3a0a, 0x01}, {0x3a0b, 0x72}, {0x3a0e, 0x01},
    {0x3a0d, 0x02}, {0x3a14, 0x02}, {0x3a15, 0xe4},
    {0x4001, 0x02}, {0x4004, 0x02}, {0x4713, 0x02}, {0x4407, 0x04},
    {0x460b, 0x37}, {0x460c, 0x20}, {0x3824, 0x04}, {0x5001, 0x83},
    {0x4005, 0x1a}, {0x3008, 0x02}, {0x3503, 0x00},
};

static const ov5640_reg8_t ov5640_mode_1080p_30fps_regs[] =
{
    {0x3008, 0x42}, {0x3035, 0x21}, {0x3036, 0x54}, {0x3c07, 0x08},
    {0x3c09, 0x1c}, {0x3c0a, 0x9c}, {0x3c0b, 0x40},
    {0x3820, 0x40}, {0x3821, 0x06}, {0x3814, 0x11}, {0x3815, 0x11},
    {0x3800, 0x00}, {0x3801, 0x00}, {0x3802, 0x00}, {0x3803, 0x00},
    {0x3804, 0x0a}, {0x3805, 0x3f}, {0x3806, 0x07}, {0x3807, 0x9f},
    {0x3808, 0x0a}, {0x3809, 0x20}, {0x380a, 0x07}, {0x380b, 0x98},
    {0x380c, 0x0b}, {0x380d, 0x1c}, {0x380e, 0x07}, {0x380f, 0xb0},
    {0x3810, 0x00}, {0x3811, 0x10}, {0x3812, 0x00}, {0x3813, 0x04},
    {0x3618, 0x04}, {0x3612, 0x29}, {0x3708, 0x21}, {0x3709, 0x12},
    {0x370c, 0x00}, {0x3a02, 0x03}, {0x3a03, 0xd8}, {0x3a08, 0x01},
    {0x3a09, 0x27}, {0x3a0a, 0x00}, {0x3a0b, 0xf6}, {0x3a0e, 0x03},
    {0x3a0d, 0x04}, {0x3a14, 0x03}, {0x3a15, 0xd8},
    {0x4001, 0x02}, {0x4004, 0x06}, {0x4713, 0x03}, {0x4407, 0x04},
    {0x460b, 0x35}, {0x460c, 0x22}, {0x3824, 0x02}, {0x5001, 0x83},
    {0x3035, 0x11}, {0x3036, 0x54}, {0x3c07, 0x07}, {0x3c08, 0x00},
    {0x3c09, 0x1c}, {0x3c0a, 0x9c}, {0x3c0b, 0x40},
    {0x3800, 0x01}, {0x3801, 0x50}, {0x3802, 0x01}, {0x3803, 0xb2},
    {0x3804, 0x08}, {0x3805, 0xef}, {0x3806, 0x05}, {0x3807, 0xf1},
    {0x3808, 0x07}, {0x3809, 0x80}, {0x380a, 0x04}, {0x380b, 0x38},
    {0x380c, 0x09}, {0x380d, 0xc4}, {0x380e, 0x04}, {0x380f, 0x60},
    {0x3612, 0x2b}, {0x3708, 0x64}, {0x3a02, 0x04}, {0x3a03, 0x60},
    {0x3a08, 0x01}, {0x3a09, 0x50}, {0x3a0a, 0x01}, {0x3a0b, 0x18},
    {0x3a0e, 0x03}, {0x3a0d, 0x04}, {0x3a14, 0x04}, {0x3a15, 0x60},
    {0x4713, 0x02}, {0x4407, 0x04}, {0x460b, 0x37}, {0x460c, 0x20},
    {0x3824, 0x04}, {0x4005, 0x1a}, {0x3008, 0x02}, {0x3503, 0x00},
};

typedef struct ov5640_mode_table
{
    const char *name;
    const ov5640_reg8_t *regs;
    rt_size_t count;
    rt_uint16_t width;
    rt_uint16_t height;
} ov5640_mode_table_t;

static const ov5640_mode_table_t ov5640_mode_tables[] =
{
    {"vga", ov5640_mode_vga_30fps_regs, sizeof(ov5640_mode_vga_30fps_regs) / sizeof(ov5640_mode_vga_30fps_regs[0]), 640, 480},
    {"qvga", ov5640_mode_qvga_30fps_regs, sizeof(ov5640_mode_qvga_30fps_regs) / sizeof(ov5640_mode_qvga_30fps_regs[0]), 320, 240},
    {"qcif", ov5640_mode_qcif_30fps_regs, sizeof(ov5640_mode_qcif_30fps_regs) / sizeof(ov5640_mode_qcif_30fps_regs[0]), 176, 144},
    {"720p", ov5640_mode_720p_30fps_regs, sizeof(ov5640_mode_720p_30fps_regs) / sizeof(ov5640_mode_720p_30fps_regs[0]), 1280, 720},
    {"1080p", ov5640_mode_1080p_30fps_regs, sizeof(ov5640_mode_1080p_30fps_regs) / sizeof(ov5640_mode_1080p_30fps_regs[0]), 1920, 1080},
};

static rt_err_t ov5640_write_regs(const ov5640_reg8_t *regs, rt_size_t count)
{
    for (rt_size_t i = 0; i < count; i++)
    {
        if (!wrSensorReg16_8(regs[i].reg, regs[i].value))
        {
            return -RT_ERROR;
        }
    }

    return RT_EOK;
}

static rt_err_t ov5640_read_u8(rt_uint16_t reg, rt_uint8_t *value)
{
    if ((value == RT_NULL) || !rdSensorReg16_8(reg, value))
    {
        return -RT_ERROR;
    }

    return RT_EOK;
}

static rt_err_t ov5640_write_u8(rt_uint16_t reg, rt_uint8_t value)
{
    return wrSensorReg16_8(reg, value) ? RT_EOK : -RT_ERROR;
}

static rt_err_t ov5640_get_u16(rt_uint16_t high_reg, rt_uint16_t low_reg, rt_uint16_t *value)
{
    rt_uint8_t high;
    rt_uint8_t low;

    if (value == RT_NULL)
    {
        return -RT_ERROR;
    }

    if ((ov5640_read_u8(high_reg, &high) != RT_EOK) ||
        (ov5640_read_u8(low_reg, &low) != RT_EOK))
    {
        return -RT_ERROR;
    }

    *value = (rt_uint16_t)(((rt_uint16_t)high << 8) | low);
    return RT_EOK;
}

static rt_err_t ov5640_get_hts(rt_uint16_t *hts)
{
    return ov5640_get_u16(0x380c, 0x380d, hts);
}

static rt_err_t ov5640_get_vts(rt_uint16_t *vts)
{
    return ov5640_get_u16(0x380e, 0x380f, vts);
}

static rt_err_t ov5640_set_vts(rt_uint16_t vts)
{
    if ((ov5640_write_u8(0x380f, (rt_uint8_t)(vts & 0xffU)) != RT_EOK) ||
        (ov5640_write_u8(0x380e, (rt_uint8_t)(vts >> 8)) != RT_EOK))
    {
        return -RT_ERROR;
    }

    return RT_EOK;
}

static rt_err_t ov5640_get_sysclk_10khz(rt_uint32_t *sysclk)
{
    static const rt_uint8_t sclk_rdiv_map[] = {1U, 2U, 4U, 8U};
    rt_uint8_t reg3034;
    rt_uint8_t reg3035;
    rt_uint8_t reg3036;
    rt_uint8_t reg3037;
    rt_uint8_t reg3108;
    rt_uint32_t bit_div2x = 1U;
    rt_uint32_t sys_div;
    rt_uint32_t pre_div;
    rt_uint32_t pll_rdiv;
    rt_uint32_t sclk_rdiv;
    rt_uint32_t vco;

    if (sysclk == RT_NULL)
    {
        return -RT_ERROR;
    }

    if ((ov5640_read_u8(0x3034, &reg3034) != RT_EOK) ||
        (ov5640_read_u8(0x3035, &reg3035) != RT_EOK) ||
        (ov5640_read_u8(0x3036, &reg3036) != RT_EOK) ||
        (ov5640_read_u8(0x3037, &reg3037) != RT_EOK) ||
        (ov5640_read_u8(0x3108, &reg3108) != RT_EOK))
    {
        return -RT_ERROR;
    }

    if (((reg3034 & 0x0fU) == 8U) || ((reg3034 & 0x0fU) == 10U))
    {
        bit_div2x = (rt_uint32_t)(reg3034 & 0x0fU) / 2U;
    }

    sys_div = (rt_uint32_t)(reg3035 >> 4);
    if (sys_div == 0U)
    {
        sys_div = 16U;
    }

    pre_div = (rt_uint32_t)(reg3037 & 0x0fU);
    if ((pre_div == 0U) || (reg3036 == 0U))
    {
        return -RT_ERROR;
    }

    pll_rdiv = (rt_uint32_t)(((reg3037 >> 4) & 0x01U) + 1U);
    sclk_rdiv = (rt_uint32_t)sclk_rdiv_map[reg3108 & 0x03U];
    vco = (OV5640_XCLK_HZ / 10000U) * (rt_uint32_t)reg3036 / pre_div;
    *sysclk = vco / sys_div / pll_rdiv * 2U / bit_div2x / sclk_rdiv;

    return RT_EOK;
}

static int ov5640_get_light_freq(void)
{
    rt_uint8_t reg3c01;
    rt_uint8_t value;

    if (ov5640_read_u8(0x3c01, &reg3c01) != RT_EOK)
    {
        return 0;
    }

    if ((reg3c01 & 0x80U) != 0U)
    {
        if (ov5640_read_u8(0x3c00, &value) != RT_EOK)
        {
            return 0;
        }

        return ((value & 0x04U) != 0U) ? 50 : 60;
    }

    if (ov5640_read_u8(0x3c0c, &value) != RT_EOK)
    {
        return 0;
    }

    return ((value & 0x01U) != 0U) ? 50 : 60;
}

rt_err_t ov5640_tuning_apply_low_cip(void)
{
    return ov5640_write_regs(ov5640_cip_low_regs,
                             sizeof(ov5640_cip_low_regs) / sizeof(ov5640_cip_low_regs[0]));
}

rt_err_t ov5640_tuning_apply_ev(int ev)
{
    const ov5640_reg8_t *regs;
    rt_size_t count;

    switch (ev)
    {
        case -2:
            regs = ov5640_ev_minus_2_regs;
            count = sizeof(ov5640_ev_minus_2_regs) / sizeof(ov5640_ev_minus_2_regs[0]);
            break;
        case -1:
            regs = ov5640_ev_minus_1_regs;
            count = sizeof(ov5640_ev_minus_1_regs) / sizeof(ov5640_ev_minus_1_regs[0]);
            break;
        case 1:
            regs = ov5640_ev_plus_1_regs;
            count = sizeof(ov5640_ev_plus_1_regs) / sizeof(ov5640_ev_plus_1_regs[0]);
            break;
        case 2:
            regs = ov5640_ev_plus_2_regs;
            count = sizeof(ov5640_ev_plus_2_regs) / sizeof(ov5640_ev_plus_2_regs[0]);
            break;
        case 3:
            regs = ov5640_ev_plus_3_regs;
            count = sizeof(ov5640_ev_plus_3_regs) / sizeof(ov5640_ev_plus_3_regs[0]);
            break;
        case 0:
        default:
            regs = ov5640_ev_0_regs;
            count = sizeof(ov5640_ev_0_regs) / sizeof(ov5640_ev_0_regs[0]);
            break;
    }

    return ov5640_write_regs(regs, count);
}

rt_err_t ov5640_tuning_set_ae_ag(rt_bool_t enable)
{
    rt_uint8_t ae_ag_ctrl;

    if (ov5640_read_u8(0x3503, &ae_ag_ctrl) != RT_EOK)
    {
        return -RT_ERROR;
    }

    if (enable == RT_TRUE)
    {
        ae_ag_ctrl &= (rt_uint8_t)~0x03U;
    }
    else
    {
        ae_ag_ctrl |= 0x03U;
    }

    return ov5640_write_u8(0x3503, ae_ag_ctrl);
}

rt_err_t ov5640_tuning_get_shutter(rt_uint32_t *shutter)
{
    rt_uint8_t high;
    rt_uint8_t middle;
    rt_uint8_t low;

    if (shutter == RT_NULL)
    {
        return -RT_ERROR;
    }

    if ((ov5640_read_u8(0x3500, &high) != RT_EOK) ||
        (ov5640_read_u8(0x3501, &middle) != RT_EOK) ||
        (ov5640_read_u8(0x3502, &low) != RT_EOK))
    {
        return -RT_ERROR;
    }

    *shutter = (((rt_uint32_t)high & 0x0fU) << 12) |
               ((rt_uint32_t)middle << 4) |
               ((rt_uint32_t)low >> 4);
    return RT_EOK;
}

rt_err_t ov5640_tuning_set_shutter(rt_uint32_t shutter)
{
    rt_uint16_t vts;

    if (shutter == 0U)
    {
        shutter = 1U;
    }
    if (shutter > OV5640_SHUTTER_MAX)
    {
        shutter = OV5640_SHUTTER_MAX;
    }

    if ((ov5640_get_vts(&vts) == RT_EOK) && ((rt_uint32_t)vts > 4U) && (shutter > ((rt_uint32_t)vts - 4U)))
    {
        (void)ov5640_set_vts((rt_uint16_t)(shutter + 4U));
    }

    if ((ov5640_write_u8(0x3502, (rt_uint8_t)((shutter & 0x0fU) << 4)) != RT_EOK) ||
        (ov5640_write_u8(0x3501, (rt_uint8_t)((shutter >> 4) & 0xffU)) != RT_EOK) ||
        (ov5640_write_u8(0x3500, (rt_uint8_t)((shutter >> 12) & 0x0fU)) != RT_EOK))
    {
        return -RT_ERROR;
    }

    return RT_EOK;
}

rt_err_t ov5640_tuning_get_gain16(rt_uint16_t *gain16)
{
    rt_uint8_t high;
    rt_uint8_t low;

    if (gain16 == RT_NULL)
    {
        return -RT_ERROR;
    }

    if ((ov5640_read_u8(0x350a, &high) != RT_EOK) ||
        (ov5640_read_u8(0x350b, &low) != RT_EOK))
    {
        return -RT_ERROR;
    }

    *gain16 = (rt_uint16_t)((((rt_uint16_t)high & 0x03U) << 8) | low);
    return RT_EOK;
}

rt_err_t ov5640_tuning_set_gain16(rt_uint16_t gain16)
{
    if (gain16 < OV5640_GAIN16_MIN)
    {
        gain16 = OV5640_GAIN16_MIN;
    }
    if (gain16 > OV5640_GAIN16_MAX)
    {
        gain16 = OV5640_GAIN16_MAX;
    }

    if ((ov5640_write_u8(0x350b, (rt_uint8_t)(gain16 & 0xffU)) != RT_EOK) ||
        (ov5640_write_u8(0x350a, (rt_uint8_t)((gain16 >> 8) & 0x03U)) != RT_EOK))
    {
        return -RT_ERROR;
    }

    return RT_EOK;
}

rt_err_t ov5640_tuning_set_ae_target(rt_uint8_t target)
{
    rt_uint16_t ae_low = ((rt_uint16_t)target * 23U) / 25U;
    rt_uint16_t ae_high = ((rt_uint16_t)target * 27U) / 25U;
    rt_uint16_t fast_high = ae_high << 1;
    rt_uint16_t fast_low = ae_low >> 1;
    ov5640_reg8_t regs[6];

    if (target == 0U)
    {
        target = OV5640_AE_TARGET_DEFAULT;
        ae_low = ((rt_uint16_t)target * 23U) / 25U;
        ae_high = ((rt_uint16_t)target * 27U) / 25U;
        fast_high = ae_high << 1;
        fast_low = ae_low >> 1;
    }

    if (fast_high > 255U)
    {
        fast_high = 255U;
    }

    regs[0].reg = 0x3a0f; regs[0].value = (rt_uint8_t)ae_high;
    regs[1].reg = 0x3a10; regs[1].value = (rt_uint8_t)ae_low;
    regs[2].reg = 0x3a1b; regs[2].value = (rt_uint8_t)ae_high;
    regs[3].reg = 0x3a1e; regs[3].value = (rt_uint8_t)ae_low;
    regs[4].reg = 0x3a11; regs[4].value = (rt_uint8_t)fast_high;
    regs[5].reg = 0x3a1f; regs[5].value = (rt_uint8_t)fast_low;

    return ov5640_write_regs(regs, sizeof(regs) / sizeof(regs[0]));
}

rt_err_t ov5640_tuning_apply_banding(int light_freq)
{
    rt_uint32_t sysclk_10khz;
    rt_uint16_t hts;
    rt_uint16_t vts;
    rt_uint32_t band_step60;
    rt_uint32_t band_step50;
    rt_uint32_t max_band60;
    rt_uint32_t max_band50;
    rt_uint8_t reg3c00;

    if ((light_freq != 50) && (light_freq != 60))
    {
        light_freq = ov5640_get_light_freq();
    }

    if ((light_freq != 50) && (light_freq != 60))
    {
        light_freq = 50;
    }

    if ((ov5640_get_sysclk_10khz(&sysclk_10khz) != RT_EOK) ||
        (ov5640_get_hts(&hts) != RT_EOK) ||
        (ov5640_get_vts(&vts) != RT_EOK) ||
        (hts == 0U))
    {
        return -RT_ERROR;
    }

    band_step60 = sysclk_10khz * 100U / (rt_uint32_t)hts * 100U / 120U;
    band_step50 = sysclk_10khz * 100U / (rt_uint32_t)hts;
    if ((band_step60 == 0U) || (band_step50 == 0U))
    {
        return -RT_ERROR;
    }

    max_band60 = ((rt_uint32_t)vts > 4U) ? (((rt_uint32_t)vts - 4U) / band_step60) : 0U;
    max_band50 = ((rt_uint32_t)vts > 4U) ? (((rt_uint32_t)vts - 4U) / band_step50) : 0U;

    if ((ov5640_write_u8(0x3a0a, (rt_uint8_t)(band_step60 >> 8)) != RT_EOK) ||
        (ov5640_write_u8(0x3a0b, (rt_uint8_t)(band_step60 & 0xffU)) != RT_EOK) ||
        (ov5640_write_u8(0x3a0d, (rt_uint8_t)max_band60) != RT_EOK) ||
        (ov5640_write_u8(0x3a08, (rt_uint8_t)(band_step50 >> 8)) != RT_EOK) ||
        (ov5640_write_u8(0x3a09, (rt_uint8_t)(band_step50 & 0xffU)) != RT_EOK) ||
        (ov5640_write_u8(0x3a0e, (rt_uint8_t)max_band50) != RT_EOK))
    {
        return -RT_ERROR;
    }

    if (ov5640_read_u8(0x3c00, &reg3c00) == RT_EOK)
    {
        if (light_freq == 50)
        {
            reg3c00 |= 0x04U;
        }
        else
        {
            reg3c00 &= (rt_uint8_t)~0x04U;
        }
        (void)ov5640_write_u8(0x3c00, reg3c00);
    }

    return RT_EOK;
}

rt_err_t ov5640_tuning_apply_sensor_mode_stopped(const char *name, rt_uint16_t *width, rt_uint16_t *height)
{
    if ((name == RT_NULL) || (name[0] == '\0'))
    {
        return -RT_ERROR;
    }

    for (rt_size_t i = 0; i < sizeof(ov5640_mode_tables) / sizeof(ov5640_mode_tables[0]); i++)
    {
        if (rt_strcmp(name, ov5640_mode_tables[i].name) == 0)
        {
            rt_err_t err;

            (void)ov5640_write_u8(0x4202, 0x0f);
            rt_thread_mdelay(5);
            err = ov5640_write_regs(ov5640_mode_tables[i].regs, ov5640_mode_tables[i].count);
            if (err != RT_EOK)
            {
                return err;
            }
            (void)ov5640_tuning_set_ae_target(OV5640_AE_TARGET_DEFAULT);
            (void)ov5640_tuning_apply_banding(0);
            rt_thread_mdelay(30);
            if (width != RT_NULL)
            {
                *width = ov5640_mode_tables[i].width;
            }
            if (height != RT_NULL)
            {
                *height = ov5640_mode_tables[i].height;
            }
            return RT_EOK;
        }
    }

    return -RT_ERROR;
}

rt_err_t ov5640_tuning_apply_sensor_mode(const char *name)
{
    rt_err_t err = ov5640_tuning_apply_sensor_mode_stopped(name, RT_NULL, RT_NULL);

    if (err == RT_EOK)
    {
        (void)ov5640_write_u8(0x4202, 0x00);
    }

    return err;
}

static void ov5640_focus_thread_entry(void *parameter)
{
    RT_UNUSED(parameter);

    if (OV5640_af_init() == RT_EOK)
    {
        rt_kprintf("[camaf] AF init OK, focus ready\n");
        ov5640_focus_ready = RT_TRUE;
        (void)OV5640_auto_focus();
    }
    else
    {
        rt_kprintf("[camaf] AF init FAILED, focus disabled\n");
    }

    while (1)
    {
        rt_sem_take(ov5640_focus_sem, RT_WAITING_FOREVER);
        rt_kprintf("[camaf] AF trigger received\n");
        if (ov5640_focus_ready == RT_TRUE)
        {
            (void)OV5640_auto_focus();
        }
    }
}

void ov5640_focus_service_start(void)
{
    if (ov5640_focus_thread != RT_NULL)
    {
        return;
    }

    if (ov5640_focus_sem == RT_NULL)
    {
        ov5640_focus_sem = rt_sem_create("camaf", 0, RT_IPC_FLAG_FIFO);
    }

    if (ov5640_focus_sem == RT_NULL)
    {
        return;
    }

    ov5640_focus_thread = rt_thread_create("camaf",
                                           ov5640_focus_thread_entry,
                                           RT_NULL,
                                           4096,
                                           12,
                                           10);
    if (ov5640_focus_thread != RT_NULL)
    {
        rt_thread_startup(ov5640_focus_thread);
    }
}

void ov5640_focus_trigger(void)
{
    if (ov5640_focus_sem != RT_NULL)
    {
        rt_sem_release(ov5640_focus_sem);
    }
}
