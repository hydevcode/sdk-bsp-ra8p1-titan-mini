#include <rtthread.h>
#include <rtdevice.h>
#include <string.h>
#include "hal_data.h"
#include <board.h>

/* FSP USB basic driver internals (usb_utr_t, class codes) */
#include "r_usb_basic.h"
#include "r_usb_basic_api.h"
#include "r_usb_basic_cfg.h"
#include "r_usb_typedef.h"
#include "r_usb_extern.h"
#include "r_usb_bitdefine.h"

#include "usb_paud.h"

#define DBG_TAG     "usb_paud"
#define DBG_LVL     DBG_INFO
#include <rtdbg.h>

/* ===================== Audio format: 48 kHz / stereo / 16-bit ===================== */
#define PAUD_SAMPLE_RATE          (48000U)
#define PAUD_CHANNELS             (2U)      /* stereo USB capture (L = R from the single PDM channel) */
#define PAUD_BITS                 (16U)
#define PAUD_BYTES_PER_SAMPLE     (PAUD_CHANNELS * PAUD_BITS / 8U)             /* 4   */
#define PAUD_BYTES_PER_MS         (PAUD_SAMPLE_RATE * PAUD_BYTES_PER_SAMPLE / 1000U) /* 192 */
#define PAUD_EP_MAX_PACKET        (PAUD_BYTES_PER_MS)                          /* 192 */

#define PAUD_PDM_RATE             (48000U)
#define PAUD_PDM_SAMPLES          (PAUD_SAMPLE_RATE * PAUD_CAP_CHUNK_MS / 1000U)         /* 960 */
#define PAUD_CAP_CHUNK_MS         (20U)
#define PAUD_CAP_FRAMES           (PAUD_SAMPLE_RATE * PAUD_CAP_CHUNK_MS / 1000U)         /* 960 */
#define PAUD_CAP_CHUNK_BYTES      (PAUD_CAP_FRAMES * PAUD_BYTES_PER_SAMPLE)              /* 3840 */
#define PAUD_CAP_RING_BYTES       (32768U)                                               /* ~170 ms */

#define PAUD_THREAD_STACK         (4096)
#define PAUD_THREAD_PRIORITY      (5)
#define PAUD_THREAD_TICK          (10)

/* ===================== State ===================== */
static volatile uint8_t g_paud_usb_ready      = 0;   /* USB configured */
static volatile uint8_t g_paud_cap_streaming  = 0;   /* capture interface active (alt=1) */
static uint32_t          g_paud_last_done_tick = 0;  /* tick of last write completion */
static volatile uint8_t  g_paud_priming       = 0;   /* 1 = app thread is re-arming */
static volatile uint8_t  g_paud_rearm_needed  = 0;   /* set by ISO write-complete ISR */
static struct rt_semaphore g_paud_iso_sem;           /* posted by ISR to wake the app thread */
static volatile uint8_t g_paud_mute           = 0;   /* host mute (stored; applied as silence) */
static uint8_t g_paud_ctrl_mute;
static uint8_t g_paud_ctrl_vol[2];
static uint8_t g_paud_ctrl_buf[4];   /* shared control-transfer data buffer (deferred write) */
static uint8_t          g_paud_volume         = 100; /* host volume 0..100 %% (UAC metadata) */

/* UAC volume range: -50 dB .. 0 dB (1/256 dB units) */
#define PAUD_VOL_MIN_DB256        (-12800)
#define PAUD_VOL_MAX_DB256        (0)
#define PAUD_VOL_RES_DB256        (256)

/* Capture: PDM block slots (ISR producer) + PCM ring (app producer, USB consumer) */
static int32_t g_paud_cap_pdm_buf[PAUD_PDM_SAMPLES]
    BSP_ALIGN_VARIABLE(32) BSP_PLACE_IN_SECTION(".ram_nocache");      /* FSP circular buffer */
static int32_t g_paud_cap_pdm_tmp[PAUD_PDM_SAMPLES] BSP_ALIGN_VARIABLE(32);  /* app-side copy */
static int16_t g_paud_cap_pcm_buf[PAUD_PDM_SAMPLES * PAUD_CHANNELS]
    BSP_ALIGN_VARIABLE(32) BSP_PLACE_IN_SECTION(".ram_nocache");
static uint8_t g_paud_cap_ring[PAUD_CAP_RING_BYTES]
    BSP_ALIGN_VARIABLE(32) BSP_PLACE_IN_SECTION(".ram_nocache");
static volatile uint16_t g_paud_cap_ring_head;  /* producer (app thread) */
static volatile uint16_t g_paud_cap_ring_tail;  /* consumer (USB write) */
static uint8_t g_paud_cap_tx_buf[PAUD_EP_MAX_PACKET]
    BSP_ALIGN_VARIABLE(32) BSP_PLACE_IN_SECTION(".ram_nocache");
static volatile uint8_t g_cap_write_pending = 0; /* an ISO IN write is in flight */
static volatile uint8_t g_pdm_restart_req   = 0;  /* PDM FIFO overflowed; resync needed */

static volatile uint32_t g_pdm_blk_ready = 0;   /* blocks completed by the ISR */
static uint32_t g_pdm_blk_seen = 0;             /* blocks already consumed by the app */
static int32_t  g_paud_cap_pdm_slot[2][PAUD_PDM_SAMPLES]
    BSP_ALIGN_VARIABLE(32) BSP_PLACE_IN_SECTION(".ram_nocache");


static int32_t paud_cap_ring_write_pcm_frames(const int16_t * pcm, uint16_t frames, int32_t adjust)
{
    uint16_t head = g_paud_cap_ring_head;
    uint16_t total = (uint16_t) ((int32_t) frames + adjust);
    uint16_t written = 0;

    rt_base_t level = rt_hw_interrupt_disable();

    for (uint16_t i = 0; i < total; i++)
    {
        int16_t l, r;
        if (0 == adjust)
        {
            const int16_t * f = &pcm[i * 2U];
            l = f[0];
            r = f[1];
        }
        else
        {
            uint32_t pos  = ((uint32_t) i * (uint32_t) frames * 256U) / (uint32_t) total;
            uint32_t idx  = pos >> 8;
            uint32_t fr   = pos & 0xFFU;
            uint32_t idx2 = (idx + 1U < (uint32_t) frames) ? (idx + 1U) : idx;
            const int16_t * f0 = &pcm[idx * 2U];
            const int16_t * f1 = &pcm[idx2 * 2U];
            l = (int16_t) (((int32_t) f0[0] * (int32_t) (256U - fr) + (int32_t) f1[0] * (int32_t) fr) / 256);
            r = (int16_t) (((int32_t) f0[1] * (int32_t) (256U - fr) + (int32_t) f1[1] * (int32_t) fr) / 256);
        }

        uint16_t next = (uint16_t) ((head + 1) & (PAUD_CAP_RING_BYTES - 1U));
        if (next == g_paud_cap_ring_tail)
        {
            break;   /* ring full: drop remainder */
        }
        g_paud_cap_ring[head] = (uint8_t) (l & 0xFF);
        head = (uint16_t) ((head + 1) & (PAUD_CAP_RING_BYTES - 1U));
        g_paud_cap_ring[head] = (uint8_t) ((l >> 8) & 0xFF);
        head = (uint16_t) ((head + 1) & (PAUD_CAP_RING_BYTES - 1U));
        g_paud_cap_ring[head] = (uint8_t) (r & 0xFF);
        head = (uint16_t) ((head + 1) & (PAUD_CAP_RING_BYTES - 1U));
        g_paud_cap_ring[head] = (uint8_t) ((r >> 8) & 0xFF);
        head = (uint16_t) ((head + 1) & (PAUD_CAP_RING_BYTES - 1U));
        written++;
    }
    g_paud_cap_ring_head = head;

    rt_hw_interrupt_enable(level);
    return written;
}

#define PAUD_RING_TARGET_BYTES   (PAUD_CAP_RING_BYTES * 3 / 4)  /* 75% */
#define PAUD_RING_ADJ_MAX        (24)   /* +/- 24 frames/block = +/-2.5% */
#define PAUD_ADJ_NOMINAL         (-8)   /* PDM ~48.43k -> USB 48k: drop ~8.5/960 */

static int32_t paud_ring_adjust_calc(void)
{
    uint16_t h = g_paud_cap_ring_head;
    uint16_t t = g_paud_cap_ring_tail;
    int32_t level = (int32_t) ((h + PAUD_CAP_RING_BYTES - t) & (PAUD_CAP_RING_BYTES - 1U));

    static int32_t s_lvl = 0;
    if (0 == s_lvl) { s_lvl = level; }
    s_lvl += (level - s_lvl) / 8;

    int32_t err_frames = (s_lvl - (int32_t) PAUD_RING_TARGET_BYTES) / (int32_t) PAUD_BYTES_PER_SAMPLE;

    int32_t adj = PAUD_ADJ_NOMINAL - (err_frames / 64);
    if (adj > PAUD_RING_ADJ_MAX) { adj = PAUD_RING_ADJ_MAX; }
    else if (adj < -PAUD_RING_ADJ_MAX) { adj = -PAUD_RING_ADJ_MAX; }
    return adj;
}

static uint16_t paud_cap_ring_read(uint8_t * out, uint16_t len)
{
    uint16_t tail = g_paud_cap_ring_tail;
    uint16_t n    = 0;

    while ((n < len) && (tail != g_paud_cap_ring_head))
    {
        out[n++] = g_paud_cap_ring[tail];
        tail = (uint16_t) ((tail + 1) & (PAUD_CAP_RING_BYTES - 1U));
    }
    g_paud_cap_ring_tail = tail;
    return n;
}

/* ===================== FSP PAUD driver callbacks ===================== */
static void paud_capture_prime(void);      /* forward decl: app-thread ISO IN re-arm */
void pdm_callback(pdm_callback_args_t * p_args)
{
    if (p_args->event == PDM_EVENT_DATA)
    {
        uint32_t * src = (uint32_t *) g_paud_cap_pdm_buf;
        uint32_t   off = (uint32_t) ((uint32_t *) g_pdm0_ctrl.p_read - src);
        uint32_t * dst = (uint32_t *) g_paud_cap_pdm_slot[g_pdm_blk_ready & 1U];
        if (off >= PAUD_PDM_SAMPLES)
        {
            off = 0U;
        }
        for (uint32_t k = 0U; k < PAUD_PDM_SAMPLES; k++)
        {
            uint32_t idx = off + k;
            if (idx >= PAUD_PDM_SAMPLES)
            {
                idx -= PAUD_PDM_SAMPLES;
            }
            dst[k] = src[idx];
        }
        g_pdm_blk_ready++;
    }
    else if (p_args->event == PDM_EVENT_ERROR)
    {
        if (p_args->error & PDM_ERROR_BUFFER_OVERWRITE)
        {
            g_pdm_restart_req = 1;
        }
    }
}

void usb_pstd_bemp_notify(void)
{
    rt_sem_release(&g_paud_iso_sem);
}
static void paud_capture_prime(void)
{
    uint16_t n;
    rt_base_t level;

    if (g_paud_priming)
    {
        return;
    }
    g_paud_priming = 1;

    level = rt_hw_interrupt_disable();
    n = paud_cap_ring_read(g_paud_cap_tx_buf, PAUD_EP_MAX_PACKET);
    if ((n < PAUD_EP_MAX_PACKET) || g_paud_mute)
    {
        rt_memset(g_paud_cap_tx_buf + n, 0, PAUD_EP_MAX_PACKET - n);
    }
    rt_hw_interrupt_enable(level);

    fsp_err_t err = R_USB_Write(&g_basic1_ctrl, g_paud_cap_tx_buf, PAUD_EP_MAX_PACKET, USB_CLASS_PAUD);
    if (FSP_SUCCESS == err)
    {
        g_cap_write_pending = 1;
    }
    else
    {
        g_cap_write_pending = 0;
        g_paud_last_done_tick = rt_tick_get();
    }

    g_paud_priming = 0;
}

#define PAUD_PDM_SETTLE_BLOCKS   (2)

static uint8_t g_paud_pdm_started = 0;   /* R_PDM running (continuous circular buffer) */
static uint8_t g_paud_pdm_settle  = 0;   /* blocks still being discarded after start */

static void paud_pdm_start(void)         /* app-thread context; never blocks the USB loop */
{
    fsp_err_t err;

    if (g_paud_pdm_started)
    {
        return;
    }

    err = R_PDM_Open(&g_pdm0_ctrl, &g_pdm0_cfg);
    if (err != FSP_SUCCESS)
    {
        LOG_E("R_PDM_Open failed: %d", err);
        return;
    }

    err = R_PDM_Start(&g_pdm0_ctrl, g_paud_cap_pdm_buf,
                      (uint32_t) sizeof(g_paud_cap_pdm_buf), PAUD_PDM_SAMPLES);
    if (err != FSP_SUCCESS)
    {
        LOG_E("R_PDM_Start failed: %d", err);
        R_PDM_Close(&g_pdm0_ctrl);
        return;
    }

    /* discard blocks produced before the app started consuming */
    g_pdm_blk_seen = g_pdm_blk_ready;

    g_paud_pdm_started = 1;
    g_paud_pdm_settle  = PAUD_PDM_SETTLE_BLOCKS;
}

static void paud_pdm_stop(void)
{
    if (!g_paud_pdm_started)
    {
        return;
    }
    g_paud_pdm_started = 0;
    g_paud_pdm_settle  = 0;
    R_PDM_Stop(&g_pdm0_ctrl);
    R_PDM_Close(&g_pdm0_ctrl);
}

static void paud_start_capture(void)
{
    if (g_paud_cap_streaming)
    {
        return;
    }
    g_paud_cap_streaming = 1;

    rt_memset(g_paud_cap_ring, 0, PAUD_CAP_RING_BYTES * 3 / 4);
    g_paud_cap_ring_head = (uint16_t) (PAUD_CAP_RING_BYTES * 3 / 4);
    g_paud_cap_ring_tail = 0;
    g_cap_write_pending  = 0;
}

static void paud_stop_capture(void)
{
    if (!g_paud_cap_streaming)
    {
        return;
    }
    g_paud_cap_streaming = 0;
    g_cap_write_pending  = 0;
    paud_pdm_stop();
}

static void paud_keep_capture_alive(void)
{
    if (!g_paud_cap_streaming)
    {
        return;
    }

    /* Recover from a PDM FIFO overflow: resync the driver with the hardware. */
    if (g_pdm_restart_req)
    {
        g_pdm_restart_req = 0;
        g_pdm_blk_seen = g_pdm_blk_ready;
    }

    if (!g_paud_pdm_started)
    {
        paud_pdm_start();
    }

    if (g_paud_rearm_needed)
    {
        g_paud_rearm_needed = 0;
        paud_capture_prime();
    }
    else if (!g_cap_write_pending &&
             ((rt_tick_get() - g_paud_last_done_tick) > rt_tick_from_millisecond(50)))
    {
        paud_capture_prime();
    }
}

/* ===================== Volume helpers ===================== */
static int16_t paud_volume_to_db256(void)
{
    int32_t pct = g_paud_volume;
    if (pct > 100) pct = 100;
    return (int16_t) (PAUD_VOL_MIN_DB256 + (PAUD_VOL_MAX_DB256 - PAUD_VOL_MIN_DB256) * pct / 100);
}

static void paud_volume_from_db256(int16_t db256)
{
    int32_t v = db256;

    if (v < PAUD_VOL_MIN_DB256) v = PAUD_VOL_MIN_DB256;
    if (v > PAUD_VOL_MAX_DB256) v = PAUD_VOL_MAX_DB256;
    g_paud_volume = (uint8_t) (((v - PAUD_VOL_MIN_DB256) * 100) / (PAUD_VOL_MAX_DB256 - PAUD_VOL_MIN_DB256));
}

/* ===================== USB audio class requests ===================== */
static void paud_handle_class_request(usb_event_info_t * info)
{
    uint16_t req_type = info->setup.request_type;              /* (bRequest << 8) | bmRequestType */
    uint8_t  bm_type  = (uint8_t) (req_type & 0xFF);           /* low byte: bmRequestType */
    uint8_t  b_req    = (uint8_t) (req_type >> 8);             /* high byte: bRequest */
    uint16_t w_value  = info->setup.request_value;
    uint16_t w_index  = info->setup.request_index;
    uint16_t w_length = info->setup.request_length;
    uint8_t  cs       = (uint8_t) (w_value >> 8);              /* control selector */
    uint8_t  recip    = (uint8_t) (bm_type & 0x0F);
    uint8_t  is_in    = (uint8_t) ((bm_type & 0x80) ? 1 : 0);

    if (((bm_type >> 5) & 0x03) != 0x01)
    {
        /* Not a class request: the control transfer must still be terminated,
         * otherwise the host times out and re-tries. */
        R_USB_PeriControlStatusSet(&g_basic1_ctrl, USB_SETUP_STATUS_STALL);
        return;
    }

    /* SET request without data: complete with ACK */
    if ((!is_in) && (w_length == 0))
    {
        R_USB_PeriControlStatusSet(&g_basic1_ctrl, USB_SETUP_STATUS_ACK);
        return;
    }

    if ((recip == 0x01) && ((w_index == 0) || ((w_index & 0xFF) == 0 && ((w_index >> 8) == 2 || (w_index >> 8) == 5))))
    {
        if (cs == 1)   /* Mute control */
        {
            if (is_in && (b_req == 0x81))   /* GET_CUR */
            {
                g_paud_ctrl_buf[0] = g_paud_mute ? 1 : 0;
                R_USB_PeriControlDataSet(&g_basic1_ctrl, g_paud_ctrl_buf, 1);
            }
            else if ((!is_in) && (b_req == 0x01) && (w_length <= 1))   /* SET_CUR */
            {
                g_paud_ctrl_mute = 0;
                if (FSP_SUCCESS == R_USB_PeriControlDataGet(&g_basic1_ctrl, &g_paud_ctrl_mute, 1))
                {
                    g_paud_mute = g_paud_ctrl_mute ? 1 : 0;
                    LOG_I("mic mute=%d", g_paud_mute);
                }
            }
            else
            {
                R_USB_PeriControlStatusSet(&g_basic1_ctrl, USB_SETUP_STATUS_STALL);
            }
        }
        else if (cs == 2)   /* Volume control */
        {
            if (is_in && (b_req == 0x81))   /* GET_CUR */
            {
                int16_t v = paud_volume_to_db256();
                g_paud_ctrl_buf[0] = (uint8_t) (v & 0xFF);
                g_paud_ctrl_buf[1] = (uint8_t) ((v >> 8) & 0xFF);
                R_USB_PeriControlDataSet(&g_basic1_ctrl, g_paud_ctrl_buf, 2);
            }
            else if ((!is_in) && (b_req == 0x01) && (w_length <= 2))   /* SET_CUR */
            {
                g_paud_ctrl_vol[0] = 0;
                g_paud_ctrl_vol[1] = 0;
                if (FSP_SUCCESS == R_USB_PeriControlDataGet(&g_basic1_ctrl, g_paud_ctrl_vol, 2))
                {
                    int16_t v = (int16_t) (g_paud_ctrl_vol[0] | (g_paud_ctrl_vol[1] << 8));
                    paud_volume_from_db256(v);
                }
            }
            else if (is_in && (b_req == 0x82))   /* GET_MIN */
            {
                int16_t v = PAUD_VOL_MIN_DB256;
                g_paud_ctrl_buf[0] = (uint8_t) (v & 0xFF);
                g_paud_ctrl_buf[1] = (uint8_t) ((v >> 8) & 0xFF);
                R_USB_PeriControlDataSet(&g_basic1_ctrl, g_paud_ctrl_buf, 2);
            }
            else if (is_in && (b_req == 0x83))   /* GET_MAX */
            {
                int16_t v = PAUD_VOL_MAX_DB256;
                g_paud_ctrl_buf[0] = (uint8_t) (v & 0xFF);
                g_paud_ctrl_buf[1] = (uint8_t) ((v >> 8) & 0xFF);
                R_USB_PeriControlDataSet(&g_basic1_ctrl, g_paud_ctrl_buf, 2);
            }
            else if (is_in && (b_req == 0x84))   /* GET_RES */
            {
                int16_t v = PAUD_VOL_RES_DB256;
                g_paud_ctrl_buf[0] = (uint8_t) (v & 0xFF);
                g_paud_ctrl_buf[1] = (uint8_t) ((v >> 8) & 0xFF);
                R_USB_PeriControlDataSet(&g_basic1_ctrl, g_paud_ctrl_buf, 2);
            }
            else
            {
                R_USB_PeriControlStatusSet(&g_basic1_ctrl, USB_SETUP_STATUS_STALL);
            }
        }
        else
        {
            R_USB_PeriControlStatusSet(&g_basic1_ctrl, USB_SETUP_STATUS_STALL);
        }
    }
    else if (cs == 1)
    {
        if ((recip == 0x01 && w_index == 1) || (recip == 0x02 && (w_index & 0xFF) == 0x81))
        {
            if (is_in && (b_req == 0x81))   /* GET_CUR */
            {
                g_paud_ctrl_buf[0] = (uint8_t) (PAUD_SAMPLE_RATE & 0xFF);
                g_paud_ctrl_buf[1] = (uint8_t) ((PAUD_SAMPLE_RATE >> 8) & 0xFF);
                g_paud_ctrl_buf[2] = (uint8_t) ((PAUD_SAMPLE_RATE >> 16) & 0xFF);
                R_USB_PeriControlDataSet(&g_basic1_ctrl, g_paud_ctrl_buf, 3);
            }
            else if ((!is_in) && (b_req == 0x01) && (w_length <= 3))   /* SET_CUR: accept (48 kHz only) */
            {
                R_USB_PeriControlDataGet(&g_basic1_ctrl, g_paud_ctrl_buf, 3);
            }
            else
            {
                R_USB_PeriControlStatusSet(&g_basic1_ctrl, USB_SETUP_STATUS_STALL);
            }
        }
        else
        {
            R_USB_PeriControlStatusSet(&g_basic1_ctrl, USB_SETUP_STATUS_STALL);
        }
    }
    else
    {
        R_USB_PeriControlStatusSet(&g_basic1_ctrl, USB_SETUP_STATUS_STALL);
    }
}

/* ===================== USB standard request completion (SET_INTERFACE) ===================== */
static void paud_handle_request_complete(usb_event_info_t * info)
{
    uint8_t b_req = (uint8_t) ((info->setup.request_type >> 8) & 0xFF);

    if (b_req == 0x0B)   /* SET_INTERFACE */
    {
        uint8_t alt  = (uint8_t) (info->setup.request_value & 0xFF);
        uint8_t intf = (uint8_t) (info->setup.request_index & 0xFF);

        if (intf == 1)
        {
            if (alt == 1)
            {
                paud_start_capture();
            }
            else
            {
                paud_stop_capture();
            }
        }
    }
}

/* ===================== App thread ===================== */
static void paud_handle_capture(void)
{
    uint16_t i;

    if (!g_paud_cap_streaming)
    {
        return;
    }

    uint32_t ready = g_pdm_blk_ready;
    if (ready == g_pdm_blk_seen)
    {
        return;
    }
    uint32_t slot = (uint32_t) ((ready - 1U) & 1U);
    g_pdm_blk_seen = ready;

    /* discard the first few blocks while the PDM filters / mic settle */
    if (g_paud_pdm_settle > 0)
    {
        g_paud_pdm_settle--;
        return;
    }

    {
        rt_base_t lv = rt_hw_interrupt_disable();
        memcpy(g_paud_cap_pdm_tmp, (const void *) g_paud_cap_pdm_slot[slot], sizeof(g_paud_cap_pdm_tmp));
        rt_hw_interrupt_enable(lv);
    }

    {
        int32_t adj = paud_ring_adjust_calc();

        for (i = 0; i < PAUD_PDM_SAMPLES; i++)
        {
            uint32_t u   = (uint32_t) g_paud_cap_pdm_tmp[i];
            int32_t  v20 = (int32_t) (u << 12) >> 12;
            int16_t  s   = (v20 > 32767) ? (int16_t) 32767 : (v20 < -32768) ? (int16_t) -32768 : (int16_t) v20;
            g_paud_cap_pcm_buf[i * 2U]      = s;
            g_paud_cap_pcm_buf[i * 2U + 1U] = s;
        }

        paud_cap_ring_write_pcm_frames(g_paud_cap_pcm_buf, PAUD_PDM_SAMPLES, adj);
    }
}

/*******************************************************************************************************************//**
 * USB PAUD application thread: drives the FSP USB stack, re-arms the ISO IN
 * write and converts PDM mic blocks into the capture ring.
 **********************************************************************************************************************/
static void usb_paud_thread_entry(void * parameter)
{
    fsp_err_t        err;
    usb_event_info_t event_info;
    usb_status_t     usb_event = USB_STATUS_NONE;

    FSP_PARAMETER_NOT_USED(parameter);

    rt_memset(&event_info, 0, sizeof(event_info));
    err = R_USB_Open(&g_basic1_ctrl, &g_basic1_cfg);
    if (err != FSP_SUCCESS)
    {
        LOG_E("R_USB_Open failed: %d", err);
        return;
    }
    LOG_I("USB PAUD opened, waiting for host...");

    while (1)
    {
        rt_sem_take(&g_paud_iso_sem, rt_tick_from_millisecond(1));

        for (uint32_t i = 0; i < 8; i++)
        {
            usb_event = USB_STATUS_NONE;
            if (FSP_SUCCESS != R_USB_EventGet(&event_info, &usb_event))
            {
                break;
            }
            if (USB_STATUS_NONE == usb_event)
            {
                continue;   /* interrupt processed, no event; keep draining */
            }

            switch (usb_event)
            {
                case USB_STATUS_CONFIGURED:
                {
                    LOG_I("USB configured");
                    g_paud_usb_ready = 1;

                    break;
                }

                case USB_STATUS_REQUEST:
                {
                    paud_handle_class_request(&event_info);
                    break;
                }

                case USB_STATUS_REQUEST_COMPLETE:
                {
                    paud_handle_request_complete(&event_info);
                    break;
                }

                case USB_STATUS_WRITE_COMPLETE:
                {
                    g_cap_write_pending = 0;
                    g_paud_last_done_tick = rt_tick_get();
                    g_paud_rearm_needed = 1;
                    break;
                }

                case USB_STATUS_DETACH:
                case USB_STATUS_SUSPEND:
                {
                    paud_stop_capture();
                    g_paud_usb_ready = 0;
                    break;
                }

                case USB_STATUS_RESUME:
                {
                    g_paud_usb_ready = 1;
                    break;
                }

                default:
                {
                    break;
                }
            }
        }

        paud_keep_capture_alive();

        /* convert PDM mic data and feed the capture ring */
        paud_handle_capture();
    }
}

/*******************************************************************************************************************//**
 * Start the USB audio (UAC1 microphone) application.
 **********************************************************************************************************************/
void usb_paud_app(void)
{
    rt_sem_init(&g_paud_iso_sem, "paud", 0, RT_IPC_FLAG_FIFO);

    rt_thread_t tid = rt_thread_create("usb_paud",
                                       usb_paud_thread_entry,
                                       RT_NULL,
                                       PAUD_THREAD_STACK,
                                       PAUD_THREAD_PRIORITY,
                                       PAUD_THREAD_TICK);
    if (tid != RT_NULL)
    {
        rt_thread_startup(tid);
        LOG_I("usb_paud thread started");
    }
    else
    {
        LOG_E("create usb_paud thread failed");
    }
}