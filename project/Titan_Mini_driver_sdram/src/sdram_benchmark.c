#include <rtthread.h>
#include <board.h>
#include "hal_data.h"

#define DBG_TAG "sdram"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>

#define EXT_SDRAM_ADDR ((uint32_t)0x68000000)
#define EXT_SDRAM_SIZE (BSP_USING_SDRAM_SIZE)

#define TEST_ADDRESS   0
#define TEST_BUF_SIZE  256

/* ============================================================ */
/* Fill + Check (16bit) */
static void FillBuff16(uint16_t pattern)
{
    uint32_t i;
    uint16_t *pBuf;
    uint32_t err_cnt = 0;

    pBuf = (uint16_t *)EXT_SDRAM_ADDR;

    /* 写满 SDRAM */
    for (i = 0; i < EXT_SDRAM_SIZE / 2; i++)
    {
        *pBuf++ = pattern;
    }

    /* 校验 */
    pBuf = (uint16_t *)EXT_SDRAM_ADDR;

    for (i = 0; i < EXT_SDRAM_SIZE / 2; i++)
    {
        uint16_t val = *pBuf++;

        if (val != pattern)
        {
            LOG_W("FillBuff16 error offset:0x%08x read:0x%04x should:0x%04x",
                  i * 2,
                  val,
                  pattern);

            if (++err_cnt >= 5)
            {
                LOG_W("Too many errors, skip round.");
                break;
            }
        }
    }
}

/* ============================================================ */
/* 写速度测试 (16bit) */
void SDRAM_WriteSpeedTest16(void)
{
    uint32_t start, end;
    uint32_t i;
    uint16_t *pBuf;
    uint32_t duration;
    uint16_t val = 0;

    FillBuff16(0x55AA);
    FillBuff16(0xAA55);

    pBuf = (uint16_t *)EXT_SDRAM_ADDR;

    start = rt_tick_get_millisecond();

    for (i = 0; i < EXT_SDRAM_SIZE / 2; i++)
    {
        *pBuf++ = val++;
    }

    end = rt_tick_get_millisecond();
    duration = end - start;

    /* 校验 */
    pBuf = (uint16_t *)EXT_SDRAM_ADDR;
    val = 0;

    for (i = 0; i < EXT_SDRAM_SIZE / 2; i++)
    {
        uint16_t read = *pBuf++;
        if (read != val)
        {
            rt_kprintf("write check error at index=%d\r\n", i);
            break;
        }
        val++;
    }

    rt_kprintf("SDRAM write duration:%dms, speed:%dMB/s\r\n",
               duration,
               (EXT_SDRAM_SIZE / 1024 / 1024 * 1000) / duration);
}
MSH_CMD_EXPORT_ALIAS(SDRAM_WriteSpeedTest16, sdram_write, "SDRAM Write Test 16bit");

/* ============================================================ */
/* 读速度测试 (16bit) */
void SDRAM_ReadSpeedTest16(void)
{
    uint32_t start, end;
    uint32_t i;
    uint16_t *pBuf;
    volatile uint16_t temp;
    uint32_t duration;

    pBuf = (uint16_t *)EXT_SDRAM_ADDR;

    start = rt_tick_get_millisecond();

    for (i = 0; i < EXT_SDRAM_SIZE / 2; i++)
    {
        temp = *pBuf++;
    }

    end = rt_tick_get_millisecond();
    duration = end - start;

    rt_kprintf("SDRAM read duration:%dms, speed:%dMB/s\r\n",
               duration,
               (EXT_SDRAM_SIZE / 1024 / 1024 * 1000) / duration);
}
MSH_CMD_EXPORT_ALIAS(SDRAM_ReadSpeedTest16, sdram_read, "SDRAM Read Test 16bit");

/* ============================================================ */
/* 小块 16bit 测试 */
static void SDRAM_ReadWriteTest16(void)
{
    uint32_t i;
    uint16_t *pBuf;

    pBuf = (uint16_t *)(EXT_SDRAM_ADDR + TEST_ADDRESS);

    for (i = 0; i < TEST_BUF_SIZE; i++)
    {
        pBuf[i] = 0xA55A;
    }

    rt_kprintf("addr:0x%08X size:%d bytes\r\n",
               EXT_SDRAM_ADDR + TEST_ADDRESS,
               TEST_BUF_SIZE * 2);

    for (i = 0; i < TEST_BUF_SIZE; i++)
    {
        rt_kprintf(" %04X", pBuf[i]);

        if ((i & 7) == 7)
            rt_kprintf("\r\n");
    }
}
MSH_CMD_EXPORT_ALIAS(SDRAM_ReadWriteTest16, sdram_rw, "SDRAM RW Test 16bit");

/* ============================================================ */
/* Burn 测试 */
static void SDRAM_BurnTest16(void)
{
    for(int i=0;i<5;i++)
    {
        SDRAM_WriteSpeedTest16();
        SDRAM_ReadSpeedTest16();
        rt_thread_mdelay(5);
    }
}
MSH_CMD_EXPORT_ALIAS(SDRAM_BurnTest16, sdram_burn, "SDRAM Burn Test 16bit");
