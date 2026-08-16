
#include <rtthread.h>
#include "hal_data.h"
#include <rtdevice.h>
#include <board.h>

#define DBG_TAG     "main"
#define DBG_LVL     DBG_INFO
#include <rtdbg.h>

#define LED_PIN_R   BSP_IO_PORT_01_PIN_09
#define LED_PIN_B   BSP_IO_PORT_01_PIN_10
#define LED_PIN_G   BSP_IO_PORT_01_PIN_08

#define LED_ON  (0)
#define LED_OFF (1)

/* ========================== USB microphone sound card (UAC1) ========================== */
#if defined(BSP_USING_USB_PAUD)

#include "usb_paud.h"
#if BSP_CFG_DCACHE_ENABLED
#include "core_cm85.h"
#endif

/* P413 = USB_CH_SEL: USB-C mux select on Titan Mini. LOW -> USBFS (Full-Speed) path. */
#define USB_CH_SEL_PIN   BSP_IO_PORT_04_PIN_13

void hal_entry(void)
{
    rt_kprintf("\n");
    rt_kprintf("********************************************************************************\n");
    rt_kprintf("*   Titan Mini USB Microphone (UAC1 capture: 48 kHz / stereo / 16-bit)          *\n");
    rt_kprintf("*   PDM mic -> USB isochronous IN -> PC                                          *\n");
    rt_kprintf("*   Connect the board USB-DEV FS port to the PC.                              *\n");
    rt_kprintf("********************************************************************************\n\n");

    rt_pin_mode(LED_PIN_R, PIN_MODE_OUTPUT);
    rt_pin_mode(LED_PIN_G, PIN_MODE_OUTPUT);
    rt_pin_mode(LED_PIN_B, PIN_MODE_OUTPUT);
    rt_pin_write(LED_PIN_R, LED_OFF);
    rt_pin_write(LED_PIN_G, LED_OFF);
    rt_pin_write(LED_PIN_B, LED_OFF);

    /* route the USB-C connector to the Full-Speed USB port (P413 LOW = USBFS) */
    rt_pin_mode(USB_CH_SEL_PIN, PIN_MODE_OUTPUT);
    rt_pin_write(USB_CH_SEL_PIN, PIN_LOW);

    /* start the USB microphone (UAC1 capture) application */
    usb_paud_app();

    while (1)
    {
        rt_pin_write(LED_PIN_G, LED_ON);
        rt_thread_mdelay(500);
        rt_pin_write(LED_PIN_G, LED_OFF);
        rt_thread_mdelay(500);
    }
}

/* ===================== PDM mic -> speaker loopback (default) ===================== */
#else

#include "pdm_loopback.h"

void hal_entry(void)
{
    rt_kprintf("\n");
    rt_kprintf("********************************************************************************\n");
    rt_kprintf("*   PDM Microphone -> Speaker Real-time Loopback (48 kHz / DMA)               *\n");
    rt_kprintf("********************************************************************************\n");
    rt_kprintf("Sample Rate: 48000 Hz, 20 ms blocks, low latency\n");
    rt_kprintf("********************************************************************************\n\n");

    rt_pin_mode(LED_PIN_R, PIN_MODE_OUTPUT);
    rt_pin_mode(LED_PIN_G, PIN_MODE_OUTPUT);
    rt_pin_mode(LED_PIN_B, PIN_MODE_OUTPUT);
    rt_pin_write(LED_PIN_R, LED_OFF);
    rt_pin_write(LED_PIN_G, LED_OFF);
    rt_pin_write(LED_PIN_B, LED_OFF);

    pdm_loopback_app();

    while (1)
    {
        rt_pin_write(LED_PIN_G, LED_ON);
        rt_thread_mdelay(500);
        rt_pin_write(LED_PIN_G, LED_OFF);
        rt_thread_mdelay(500);
    }
}

#endif /* BSP_USING_USB_PAUD */
