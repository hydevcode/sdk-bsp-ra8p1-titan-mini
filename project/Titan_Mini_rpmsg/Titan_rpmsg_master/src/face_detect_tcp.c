#include "face_detect_tcp.h"

#include <rtthread.h>
#include <string.h>

#include <board.h>
#include "hal_data.h"
#include "model.h"
#include "sub_0000_tensors.h"
#include "tcm_sections.h"
#include "yolo_rtthread.h"

#define FACE_DETECT_BOX_COLOR_RGB565  (0x07E0U)
#define FACE_DETECT_BOX_THICKNESS     (3)
#define FACE_DETECT_CACHE_LINE        (32U)

static rt_bool_t face_detect_ready = RT_FALSE;
static int8_t face_input[INPUT_SIZE] BSP_ALIGN_VARIABLE(32) TCM_DTCM_DATA;
static float face_output1[GRID_SIZE_1 * GRID_SIZE_1 * ANCHORS * (5 + CLASS_NUM)] BSP_ALIGN_VARIABLE(32) TCM_DTCM_DATA;
static float face_output2[GRID_SIZE_2 * GRID_SIZE_2 * ANCHORS * (5 + CLASS_NUM)] BSP_ALIGN_VARIABLE(32) TCM_DTCM_DATA;
static det_box_t face_boxes[540] BSP_ALIGN_VARIABLE(32) TCM_DTCM_DATA;

static TCM_ITCM_CODE void face_cache_clean(void *ptr, rt_size_t len)
{
    uintptr_t start = (uintptr_t)ptr & ~(uintptr_t)(FACE_DETECT_CACHE_LINE - 1U);
    uintptr_t end = ((uintptr_t)ptr + len + FACE_DETECT_CACHE_LINE - 1U) &
                    ~(uintptr_t)(FACE_DETECT_CACHE_LINE - 1U);

    SCB_CleanDCache_by_Addr((uint32_t *)start, (int32_t)(end - start));
}

static TCM_ITCM_CODE void face_cache_invalidate(void *ptr, rt_size_t len)
{
    uintptr_t start = (uintptr_t)ptr & ~(uintptr_t)(FACE_DETECT_CACHE_LINE - 1U);
    uintptr_t end = ((uintptr_t)ptr + len + FACE_DETECT_CACHE_LINE - 1U) &
                    ~(uintptr_t)(FACE_DETECT_CACHE_LINE - 1U);

    SCB_InvalidateDCache_by_Addr((uint32_t *)start, (int32_t)(end - start));
}

static TCM_ITCM_CODE void face_cache_clean_frame_region(uint16_t *frame, int width, int x1, int y1, int x2, int y2)
{
    if ((frame == RT_NULL) || (width <= 0) || (x2 < x1) || (y2 < y1))
    {
        return;
    }

    for (int y = y1; y <= y2; y++)
    {
        uintptr_t start = (uintptr_t)(frame + ((rt_size_t)y * (rt_size_t)width) + x1);
        uintptr_t end = (uintptr_t)(frame + ((rt_size_t)y * (rt_size_t)width) + x2 + 1);

        start &= ~(uintptr_t)(FACE_DETECT_CACHE_LINE - 1U);
        end = (end + FACE_DETECT_CACHE_LINE - 1U) & ~(uintptr_t)(FACE_DETECT_CACHE_LINE - 1U);
        SCB_CleanDCache_by_Addr((uint32_t *)start, (int32_t)(end - start));
    }

    __DSB();
    __ISB();
}

static TCM_ITCM_CODE void face_draw_hline(uint16_t *frame, int width, int height, int x1, int x2, int y, uint16_t color)
{
    if ((y < 0) || (y >= height))
    {
        return;
    }

    x1 = CLAMP(x1, 0, width - 1);
    x2 = CLAMP(x2, 0, width - 1);
    if (x2 < x1)
    {
        int tmp = x1;
        x1 = x2;
        x2 = tmp;
    }

    uint16_t *row = frame + ((rt_size_t)y * (rt_size_t)width);
    for (int x = x1; x <= x2; x++)
    {
        row[x] = color;
    }
}

static TCM_ITCM_CODE void face_draw_vline(uint16_t *frame, int width, int height, int x, int y1, int y2, uint16_t color)
{
    if ((x < 0) || (x >= width))
    {
        return;
    }

    y1 = CLAMP(y1, 0, height - 1);
    y2 = CLAMP(y2, 0, height - 1);
    if (y2 < y1)
    {
        int tmp = y1;
        y1 = y2;
        y2 = tmp;
    }

    for (int y = y1; y <= y2; y++)
    {
        frame[((rt_size_t)y * (rt_size_t)width) + x] = color;
    }
}

static TCM_ITCM_CODE void face_draw_box(uint16_t *frame, int width, int height, det_box_t const *box)
{
    int x1 = CLAMP((int)box->x1, 0, width - 1);
    int y1 = CLAMP((int)box->y1, 0, height - 1);
    int x2 = CLAMP((int)box->x2, 0, width - 1);
    int y2 = CLAMP((int)box->y2, 0, height - 1);

    for (int i = 0; i < FACE_DETECT_BOX_THICKNESS; i++)
    {
        face_draw_hline(frame, width, height, x1, x2, y1 + i, FACE_DETECT_BOX_COLOR_RGB565);
        face_draw_hline(frame, width, height, x1, x2, y2 - i, FACE_DETECT_BOX_COLOR_RGB565);
        face_draw_vline(frame, width, height, x1 + i, y1, y2, FACE_DETECT_BOX_COLOR_RGB565);
        face_draw_vline(frame, width, height, x2 - i, y1, y2, FACE_DETECT_BOX_COLOR_RGB565);
    }
}

int face_detect_tcp_init(void)
{
    int16_t status;

    status = RM_ETHOSU_Open(&g_rm_ethosu0_ctrl, &g_rm_ethosu0_cfg);
    if (status != FSP_SUCCESS)
    {
        return -RT_ERROR;
    }

    face_detect_ready = RT_TRUE;
    return RT_EOK;
}

TCM_ITCM_CODE int face_detect_tcp_process_rgb565(uint16_t *frame, int width, int height)
{
    int8_t *input;
    int8_t *output1;
    int8_t *output2;
    int16_t total = 0;
    int16_t kept;
    int16_t draw_count;

    if ((face_detect_ready != RT_TRUE) || (frame == RT_NULL) || (width <= 0) || (height <= 0))
    {
        return 0;
    }

    rgb565_to_gray_resize_192_and_quantization_fast(frame, (int16_t)width, (int16_t)height, face_input);

    input = GetModelInputPtr_serving_default_image_input_0();
    memcpy(input, face_input, INPUT_SIZE);
    face_cache_clean(input, INPUT_SIZE);

    if (RunModel(false) != 0)
    {
        return -RT_ERROR;
    }

    output1 = GetModelOutputPtr_StatefulPartitionedCall_0_70273();
    output2 = GetModelOutputPtr_StatefulPartitionedCall_1_70283();
    face_cache_invalidate(output1, output1_len);
    face_cache_invalidate(output2, output2_len);

    dequantize_int8(output1, face_output1, output1_len, scale_out1, zero_point_out1);
    dequantize_int8(output2, face_output2, output2_len, scale_out2, zero_point_out2);

    total += decode_output_layer(face_output1, GRID_SIZE_1, 0, (int16_t)width, (int16_t)height,
                                 CONF_THRESH, face_boxes + total,
                                 (int16_t)(sizeof(face_boxes) / sizeof(face_boxes[0])) - total);
    total += decode_output_layer(face_output2, GRID_SIZE_2, 1, (int16_t)width, (int16_t)height,
                                 CONF_THRESH, face_boxes + total,
                                 (int16_t)(sizeof(face_boxes) / sizeof(face_boxes[0])) - total);

    kept = nms_filter(face_boxes, total, NMS_THRESH);
    draw_count = MIN(kept, MAX_BOXES);

    int dirty_x1 = width;
    int dirty_y1 = height;
    int dirty_x2 = -1;
    int dirty_y2 = -1;

    for (int16_t i = 0; i < draw_count; i++)
    {
        int x1 = CLAMP((int)face_boxes[i].x1, 0, width - 1);
        int y1 = CLAMP((int)face_boxes[i].y1, 0, height - 1);
        int x2 = CLAMP((int)face_boxes[i].x2, 0, width - 1);
        int y2 = CLAMP((int)face_boxes[i].y2, 0, height - 1);

        dirty_x1 = MIN(dirty_x1, x1);
        dirty_y1 = MIN(dirty_y1, y1);
        dirty_x2 = MAX(dirty_x2, x2);
        dirty_y2 = MAX(dirty_y2, y2);
        face_draw_box(frame, width, height, &face_boxes[i]);
    }

    if (draw_count > 0)
    {
        face_cache_clean_frame_region(frame, width, dirty_x1, dirty_y1, dirty_x2, dirty_y2);
    }

    return draw_count;
}
