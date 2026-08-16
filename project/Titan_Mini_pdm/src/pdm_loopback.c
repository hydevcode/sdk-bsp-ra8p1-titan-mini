/***********************************************************************************************************************
 * pdm_loopback.c - PDM microphone -> speaker real-time loopback
 *
 * 48 kHz / DMA continuous pipeline:
 *   PDM-IF + DMAC1 fill a small circular buffer -> pdm_callback (ISR) copies
 *   each 20 ms block into a ping-pong slot -> the loopback thread converts
 *   20-bit to 16-bit and streams it to sound0 (ES8156).
 *
 * Low latency: ~20-40 ms end to end (one block of buffering + playback).
 **********************************************************************************************************************/
#include <rtthread.h>
#include <rtdevice.h>
#include <string.h>
#include "hal_data.h"
#include <board.h>

#include "pdm_loopback.h"

#define DBG_TAG     "pdm_lb"
#define DBG_LVL     DBG_INFO
#include <rtdbg.h>

#define PDM_FS_HZ           (48000U)
#define BLOCK_MS            (20U)
#define BLOCK_SAMPLES       (PDM_FS_HZ * BLOCK_MS / 1000U)   /* 960 samples / 20 ms */
#define BLOCK_BYTES         (BLOCK_SAMPLES * sizeof(int32_t))
#define DAC_BYTES           (BLOCK_SAMPLES * 2U * 2U)        /* 3840 B stereo 16-bit */
#define NUM_SLOTS           (2U)
#define SETTLE_BLOCKS       (2U)                             /* discard first blocks */

#define AUDIO_THREAD_STACK  (4096)
#define AUDIO_THREAD_PRIO   (8)
#define AUDIO_THREAD_TICK   (10)
#define SOUND0_VOLUME       (70)

#if !defined(BSP_USING_USB_PAUD)

/* PDM DMA circular buffer (CPU reads it after DMA -> keep out of cache). */
static int32_t g_pdm_buf[BLOCK_SAMPLES]
    BSP_ALIGN_VARIABLE(32) BSP_PLACE_IN_SECTION(".ram_nocache");
/* ISR -> thread handoff (ping-pong). */
static int32_t g_slot[NUM_SLOTS][BLOCK_SAMPLES]
    BSP_ALIGN_VARIABLE(32) BSP_PLACE_IN_SECTION(".ram_nocache");
/* Converted stereo block to feed the speaker. */
static int16_t g_dac[BLOCK_SAMPLES * 2U]
    BSP_ALIGN_VARIABLE(32) BSP_PLACE_IN_SECTION(".ram_nocache");

static volatile uint32_t g_blk_ready = 0;
static rt_device_t       g_sound     = RT_NULL;

/* ===================== PDM callback (ISR context) ===================== */
void pdm_callback(pdm_callback_args_t * p_args)
{
    if (p_args->event == PDM_EVENT_DATA)
    {
        /* Copy the just-completed block out of the DMA circular buffer so the
         * next DMA block can overwrite it safely. */
        uint32_t * src = (uint32_t *) g_pdm_buf;
        uint32_t   off = (uint32_t) ((uint32_t *) g_pdm0_ctrl.p_read - src);
        uint32_t * dst = (uint32_t *) g_slot[g_blk_ready & 1U];
        if (off >= BLOCK_SAMPLES)
        {
            off = 0U;
        }
        for (uint32_t k = 0U; k < BLOCK_SAMPLES; k++)
        {
            uint32_t idx = off + k;
            if (idx >= BLOCK_SAMPLES)
            {
                idx -= BLOCK_SAMPLES;
            }
            dst[k] = src[idx];
        }
        g_blk_ready++;
    }
    else if (p_args->event == PDM_EVENT_ERROR)
    {
        LOG_E("PDM error: 0x%x", (unsigned) p_args->error);
    }
}

/* ===================== 20-bit -> 16-bit ===================== */
static inline int16_t pdm_to_16(int32_t v)
{
    int32_t s = (int32_t) ((uint32_t) v << 12) >> 12;    /* sign-extend 20-bit */
    if (s > 32767)  { s = 32767;  }
    if (s < -32768) { s = -32768; }
    return (int16_t) s;
}

/* ===================== loopback thread ===================== */
static void audio_loopback_thread_entry(void * parameter)
{
    FSP_PARAMETER_NOT_USED(parameter);

    fsp_err_t err = R_PDM_Open(&g_pdm0_ctrl, &g_pdm0_cfg);
    if (err != FSP_SUCCESS)
    {
        LOG_E("R_PDM_Open failed: %d", err);
        return;
    }

    R_BSP_SoftwareDelay(841 + 35000, BSP_DELAY_UNITS_MICROSECONDS);

    g_sound = rt_device_find("sound0");
    if (g_sound != RT_NULL)
    {
        if (rt_device_open(g_sound, RT_DEVICE_OFLAG_WRONLY) == RT_EOK)
        {
            struct rt_audio_caps caps;
            caps.main_type = AUDIO_TYPE_MIXER;
            caps.sub_type  = AUDIO_MIXER_VOLUME;
            caps.udata.value = SOUND0_VOLUME;
            if (rt_device_control(g_sound, AUDIO_CTL_CONFIGURE, &caps) != RT_EOK)
            {
                LOG_W("Failed to set speaker volume");
            }

            caps.main_type = AUDIO_TYPE_OUTPUT;
            caps.sub_type  = AUDIO_DSP_PARAM;
            caps.udata.config.samplerate = PDM_FS_HZ;
            caps.udata.config.channels   = 2;
            caps.udata.config.samplebits = 16;
            rt_device_control(g_sound, AUDIO_CTL_CONFIGURE, &caps);
            LOG_I("Speaker opened (%d Hz)", (int) PDM_FS_HZ);
        }
        else
        {
            LOG_W("Failed to open speaker");
            g_sound = RT_NULL;
        }
    }
    else
    {
        LOG_W("Speaker device not found");
    }

    g_blk_ready = 0;
    uint32_t seen   = 0;
    uint32_t settle = SETTLE_BLOCKS;

    err = R_PDM_Start(&g_pdm0_ctrl, g_pdm_buf, sizeof(g_pdm_buf), BLOCK_SAMPLES);
    if (err != FSP_SUCCESS)
    {
        LOG_E("R_PDM_Start failed: %d", err);
        goto cleanup;
    }

    while (1)
    {
        uint32_t ready = g_blk_ready;
        if (ready == seen)
        {
            rt_thread_mdelay(1);
            continue;
        }

        uint32_t slot = (uint32_t) ((ready - 1U) & 1U);   /* newest block */
        seen = ready;

        if (settle > 0)
        {
            settle--;
            continue;
        }

        /* 20-bit -> 16-bit, mono -> stereo */
        for (uint32_t i = 0; i < BLOCK_SAMPLES; i++)
        {
            int16_t s = pdm_to_16(g_slot[slot][i]);
            g_dac[i * 2U]     = s;
            g_dac[i * 2U + 1U] = s;
        }

        /* stream to the speaker (paced by sound0 consumption) */
        if (g_sound != RT_NULL)
        {
            rt_size_t offset = 0;
            while (offset < DAC_BYTES)
            {
                rt_size_t remaining = DAC_BYTES - offset;
                rt_size_t write_size = (remaining < 512) ? remaining : 512;

                rt_size_t n = rt_device_write(g_sound, 0,
                                              (uint8_t *) g_dac + offset, write_size);
                if (n > 0)
                {
                    offset += n;
                }
                else
                {
                    rt_thread_mdelay(1);
                }
            }
        }
    }

cleanup:
    R_PDM_Close(&g_pdm0_ctrl);
    if (g_sound != RT_NULL)
    {
        rt_device_close(g_sound);
    }
    LOG_E("Audio loopback thread exited");
}

/* ===================== entry ===================== */
void pdm_loopback_app(void)
{
    rt_thread_t tid = rt_thread_create("audio_lb",
                                       audio_loopback_thread_entry,
                                       RT_NULL,
                                       AUDIO_THREAD_STACK,
                                       AUDIO_THREAD_PRIO,
                                       AUDIO_THREAD_TICK);
    if (tid != RT_NULL)
    {
        rt_thread_startup(tid);
        LOG_I("PDM loopback thread started (48 kHz, 20 ms)");
    }
    else
    {
        LOG_E("Create PDM loopback thread failed");
    }
}

#endif /* !BSP_USING_USB_PAUD */
