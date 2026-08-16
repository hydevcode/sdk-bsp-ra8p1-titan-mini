#include "handwriting_model.h"

#include <rtthread.h>
#include <string.h>

#include <board.h>
#include "hal_data.h"
#include "model.h"
#include "model_select.h"
#include "sub_0000_tensors.h"
#include "tcm_sections.h"

#if TITAN_USING_HANDWRITING_MODEL

#define HANDWRITING_MODEL_CACHE_LINE  (32U)
#define HANDWRITING_OVERLAY_BG_RGB565 (0x0000U)
#define HANDWRITING_OVERLAY_FG_RGB565 (0xFFFFU)
#define HANDWRITING_PREVIEW_GRID      (64)
#define HANDWRITING_DIGIT_CANVAS      (20)
#define HANDWRITING_DIGIT_OFFSET      ((28 - HANDWRITING_DIGIT_CANVAS) / 2)
#define HANDWRITING_MIN_CONTRAST      (20)
#define HANDWRITING_MIN_FOREGROUND    (8)
#define HANDWRITING_BBOX_MARGIN_PCT   (25)
#define HANDWRITING_VOTE_HISTORY      (5)
#define HANDWRITING_INPUT_PREVIEW_SCALE (4)
#define HANDWRITING_INPUT_PREVIEW_GAP   (4)
#define HANDWRITING_ROI_SMOOTH_OLD      (3)
#define HANDWRITING_ROI_SMOOTH_NEW      (1)
#define HANDWRITING_ROI_HOLD_FRAMES     (12)

#ifndef HANDWRITING_MODEL_INVERT_INPUT
#define HANDWRITING_MODEL_INVERT_INPUT 1
#endif

#ifndef HANDWRITING_SHOW_INPUT_PREVIEW
#define HANDWRITING_SHOW_INPUT_PREVIEW 1
#endif

static rt_bool_t handwriting_model_ready = RT_FALSE;
static int8_t handwriting_input[HANDWRITING_MODEL_INPUT_SIZE] BSP_ALIGN_VARIABLE(32) TCM_DTCM_DATA;
static uint8_t handwriting_vote_history[HANDWRITING_VOTE_HISTORY];
static uint8_t handwriting_vote_index;
static uint8_t handwriting_vote_count;
static rt_bool_t handwriting_roi_valid = RT_FALSE;
static uint16_t handwriting_roi_rotation;
static int handwriting_roi_view_width;
static int handwriting_roi_view_height;
static int handwriting_roi_x;
static int handwriting_roi_y;
static int handwriting_roi_size;
static uint8_t handwriting_roi_miss_count;

static const uint8_t handwriting_digit_font[10][7] =
{
    {0x0EU, 0x11U, 0x13U, 0x15U, 0x19U, 0x11U, 0x0EU},
    {0x04U, 0x0CU, 0x04U, 0x04U, 0x04U, 0x04U, 0x0EU},
    {0x0EU, 0x11U, 0x01U, 0x02U, 0x04U, 0x08U, 0x1FU},
    {0x1FU, 0x02U, 0x04U, 0x02U, 0x01U, 0x11U, 0x0EU},
    {0x02U, 0x06U, 0x0AU, 0x12U, 0x1FU, 0x02U, 0x02U},
    {0x1FU, 0x10U, 0x1EU, 0x01U, 0x01U, 0x11U, 0x0EU},
    {0x06U, 0x08U, 0x10U, 0x1EU, 0x11U, 0x11U, 0x0EU},
    {0x1FU, 0x01U, 0x02U, 0x04U, 0x08U, 0x08U, 0x08U},
    {0x0EU, 0x11U, 0x11U, 0x0EU, 0x11U, 0x11U, 0x0EU},
    {0x0EU, 0x11U, 0x11U, 0x0FU, 0x01U, 0x02U, 0x0CU},
};

static void handwriting_cache_clean(void *ptr, rt_size_t len)
{
    uintptr_t start = (uintptr_t)ptr & ~(uintptr_t)(HANDWRITING_MODEL_CACHE_LINE - 1U);
    uintptr_t end = ((uintptr_t)ptr + len + HANDWRITING_MODEL_CACHE_LINE - 1U) &
                    ~(uintptr_t)(HANDWRITING_MODEL_CACHE_LINE - 1U);

    SCB_CleanDCache_by_Addr((uint32_t *)start, (int32_t)(end - start));
}

static void handwriting_cache_invalidate(void *ptr, rt_size_t len)
{
    uintptr_t start = (uintptr_t)ptr & ~(uintptr_t)(HANDWRITING_MODEL_CACHE_LINE - 1U);
    uintptr_t end = ((uintptr_t)ptr + len + HANDWRITING_MODEL_CACHE_LINE - 1U) &
                    ~(uintptr_t)(HANDWRITING_MODEL_CACHE_LINE - 1U);

    SCB_InvalidateDCache_by_Addr((uint32_t *)start, (int32_t)(end - start));
}

static void handwriting_cache_clean_frame_region(uint16_t *frame, int width, int x1, int y1, int x2, int y2)
{
    if ((frame == RT_NULL) || (width <= 0) || (x2 < x1) || (y2 < y1))
    {
        return;
    }

    for (int y = y1; y <= y2; y++)
    {
        uintptr_t start = (uintptr_t)(frame + ((rt_size_t)y * (rt_size_t)width) + x1);
        uintptr_t end = (uintptr_t)(frame + ((rt_size_t)y * (rt_size_t)width) + x2 + 1);

        start &= ~(uintptr_t)(HANDWRITING_MODEL_CACHE_LINE - 1U);
        end = (end + HANDWRITING_MODEL_CACHE_LINE - 1U) & ~(uintptr_t)(HANDWRITING_MODEL_CACHE_LINE - 1U);
        SCB_CleanDCache_by_Addr((uint32_t *)start, (int32_t)(end - start));
    }

    __DSB();
    __ISB();
}

static uint8_t handwriting_rgb565_to_gray(uint16_t pixel)
{
    uint16_t r = (uint16_t)((pixel >> 11) & 0x1FU);
    uint16_t g = (uint16_t)((pixel >> 5) & 0x3FU);
    uint16_t b = (uint16_t)(pixel & 0x1FU);

    r = (uint16_t)((r << 3) | (r >> 2));
    g = (uint16_t)((g << 2) | (g >> 4));
    b = (uint16_t)((b << 3) | (b >> 2));

    return (uint8_t)(((77U * r) + (150U * g) + (29U * b)) >> 8);
}

static int handwriting_clamp_int(int value, int lo, int hi)
{
    if (value < lo)
    {
        return lo;
    }
    if (value > hi)
    {
        return hi;
    }
    return value;
}

static int8_t handwriting_gray_to_model_input(uint8_t gray)
{
#if HANDWRITING_MODEL_INVERT_INPUT
    return (int8_t)(127 - (int)gray);
#else
    return (int8_t)((int)gray - 128);
#endif
}

static int8_t handwriting_model_background_value(void)
{
#if HANDWRITING_MODEL_INVERT_INPUT
    return -128;
#else
    return 127;
#endif
}

static uint8_t handwriting_model_input_to_gray(int8_t value)
{
#if HANDWRITING_MODEL_INVERT_INPUT
    return (uint8_t)(127 - (int)value);
#else
    return (uint8_t)((int)value + 128);
#endif
}

static uint16_t handwriting_gray_to_rgb565(uint8_t gray)
{
    uint16_t r = (uint16_t)(gray >> 3);
    uint16_t g = (uint16_t)(gray >> 2);
    uint16_t b = (uint16_t)(gray >> 3);

    return (uint16_t)((r << 11) | (g << 5) | b);
}

static uint16_t const *handwriting_rotated_view_pixel(uint16_t const *frame,
                                                      int width,
                                                      int height,
                                                      int view_x,
                                                      int view_y,
                                                      uint16_t rotation)
{
    int src_x;
    int src_y;

    switch (rotation)
    {
    case 90U:
        src_x = view_y;
        src_y = height - 1 - view_x;
        break;
    case 180U:
        src_x = width - 1 - view_x;
        src_y = height - 1 - view_y;
        break;
    case 270U:
        src_x = width - 1 - view_y;
        src_y = view_x;
        break;
    default:
        src_x = view_x;
        src_y = view_y;
        break;
    }

    return frame + ((rt_size_t)src_y * (rt_size_t)width) + src_x;
}

static rt_bool_t handwriting_find_digit_roi(uint16_t const *frame,
                                            int width,
                                            int height,
                                            uint16_t rotation,
                                            int *roi_x,
                                            int *roi_y,
                                            int *roi_size)
{
    int view_width = ((rotation == 90U) || (rotation == 270U)) ? height : width;
    int view_height = ((rotation == 90U) || (rotation == 270U)) ? width : height;
    int crop = (view_width < view_height) ? view_width : view_height;
    int crop_x = (view_width - crop) / 2;
    int crop_y = (view_height - crop) / 2;
    int min_gray = 255;
    int max_gray = 0;
    int min_x = HANDWRITING_PREVIEW_GRID;
    int min_y = HANDWRITING_PREVIEW_GRID;
    int max_x = -1;
    int max_y = -1;
    int foreground = 0;

    if ((frame == RT_NULL) || (crop <= 0))
    {
        return RT_FALSE;
    }

    for (int gy = 0; gy < HANDWRITING_PREVIEW_GRID; gy++)
    {
        int view_y = crop_y + (((gy * crop) + (crop / 2)) / HANDWRITING_PREVIEW_GRID);

        for (int gx = 0; gx < HANDWRITING_PREVIEW_GRID; gx++)
        {
            int view_x = crop_x + (((gx * crop) + (crop / 2)) / HANDWRITING_PREVIEW_GRID);
            int gray = handwriting_rgb565_to_gray(*handwriting_rotated_view_pixel(frame,
                                                                                  width,
                                                                                  height,
                                                                                  view_x,
                                                                                  view_y,
                                                                                  rotation));
            if (gray < min_gray)
            {
                min_gray = gray;
            }
            if (gray > max_gray)
            {
                max_gray = gray;
            }
        }
    }

    if ((max_gray - min_gray) < HANDWRITING_MIN_CONTRAST)
    {
        return RT_FALSE;
    }

    int threshold = min_gray + (((max_gray - min_gray) * 45) / 100);
    for (int gy = 0; gy < HANDWRITING_PREVIEW_GRID; gy++)
    {
        int view_y = crop_y + (((gy * crop) + (crop / 2)) / HANDWRITING_PREVIEW_GRID);

        for (int gx = 0; gx < HANDWRITING_PREVIEW_GRID; gx++)
        {
            int view_x = crop_x + (((gx * crop) + (crop / 2)) / HANDWRITING_PREVIEW_GRID);
            int gray = handwriting_rgb565_to_gray(*handwriting_rotated_view_pixel(frame,
                                                                                  width,
                                                                                  height,
                                                                                  view_x,
                                                                                  view_y,
                                                                                  rotation));
            if (gray <= threshold)
            {
                foreground++;
                if (gx < min_x)
                {
                    min_x = gx;
                }
                if (gy < min_y)
                {
                    min_y = gy;
                }
                if (gx > max_x)
                {
                    max_x = gx;
                }
                if (gy > max_y)
                {
                    max_y = gy;
                }
            }
        }
    }

    if ((foreground < HANDWRITING_MIN_FOREGROUND) ||
        (foreground > ((HANDWRITING_PREVIEW_GRID * HANDWRITING_PREVIEW_GRID) * 7 / 10)) ||
        (max_x < min_x) || (max_y < min_y))
    {
        return RT_FALSE;
    }

    int x1 = crop_x + ((min_x * crop) / HANDWRITING_PREVIEW_GRID);
    int y1 = crop_y + ((min_y * crop) / HANDWRITING_PREVIEW_GRID);
    int x2 = crop_x + ((((max_x + 1) * crop) + HANDWRITING_PREVIEW_GRID - 1) / HANDWRITING_PREVIEW_GRID) - 1;
    int y2 = crop_y + ((((max_y + 1) * crop) + HANDWRITING_PREVIEW_GRID - 1) / HANDWRITING_PREVIEW_GRID) - 1;
    int box_w = x2 - x1 + 1;
    int box_h = y2 - y1 + 1;
    int box_size = (box_w > box_h) ? box_w : box_h;
    int margin = ((box_size * HANDWRITING_BBOX_MARGIN_PCT) / 100) + 2;
    int size = box_size + (2 * margin);
    int center_x = (x1 + x2) / 2;
    int center_y = (y1 + y2) / 2;

    if (size > crop)
    {
        size = crop;
    }
    if (size < (crop / 10))
    {
        size = crop / 10;
    }
    if (size <= 0)
    {
        return RT_FALSE;
    }

    *roi_x = handwriting_clamp_int(center_x - (size / 2), 0, view_width - size);
    *roi_y = handwriting_clamp_int(center_y - (size / 2), 0, view_height - size);
    *roi_size = size;
    return RT_TRUE;
}

static void handwriting_reset_roi_if_view_changed(int view_width, int view_height, uint16_t rotation)
{
    if ((handwriting_roi_valid == RT_TRUE) &&
        (handwriting_roi_view_width == view_width) &&
        (handwriting_roi_view_height == view_height) &&
        (handwriting_roi_rotation == rotation))
    {
        return;
    }

    handwriting_roi_valid = RT_FALSE;
    handwriting_roi_rotation = rotation;
    handwriting_roi_view_width = view_width;
    handwriting_roi_view_height = view_height;
    handwriting_roi_x = 0;
    handwriting_roi_y = 0;
    handwriting_roi_size = 0;
    handwriting_roi_miss_count = 0U;
}

static void handwriting_smooth_roi(int view_width,
                                   int view_height,
                                   uint16_t rotation,
                                   rt_bool_t found,
                                   int *roi_x,
                                   int *roi_y,
                                   int *roi_size)
{
    handwriting_reset_roi_if_view_changed(view_width, view_height, rotation);

    if (found == RT_TRUE)
    {
        if (handwriting_roi_valid != RT_TRUE)
        {
            handwriting_roi_x = *roi_x;
            handwriting_roi_y = *roi_y;
            handwriting_roi_size = *roi_size;
            handwriting_roi_valid = RT_TRUE;
        }
        else
        {
            int divisor = HANDWRITING_ROI_SMOOTH_OLD + HANDWRITING_ROI_SMOOTH_NEW;

            handwriting_roi_x = ((handwriting_roi_x * HANDWRITING_ROI_SMOOTH_OLD) +
                                 (*roi_x * HANDWRITING_ROI_SMOOTH_NEW) +
                                 (divisor / 2)) / divisor;
            handwriting_roi_y = ((handwriting_roi_y * HANDWRITING_ROI_SMOOTH_OLD) +
                                 (*roi_y * HANDWRITING_ROI_SMOOTH_NEW) +
                                 (divisor / 2)) / divisor;
            handwriting_roi_size = ((handwriting_roi_size * HANDWRITING_ROI_SMOOTH_OLD) +
                                    (*roi_size * HANDWRITING_ROI_SMOOTH_NEW) +
                                    (divisor / 2)) / divisor;
        }

        handwriting_roi_miss_count = 0U;
    }
    else if (handwriting_roi_valid == RT_TRUE)
    {
        if (handwriting_roi_miss_count < HANDWRITING_ROI_HOLD_FRAMES)
        {
            handwriting_roi_miss_count++;
        }
        else
        {
            handwriting_roi_valid = RT_FALSE;
        }
    }

    if (handwriting_roi_valid == RT_TRUE)
    {
        handwriting_roi_size = handwriting_clamp_int(handwriting_roi_size, 1, view_width < view_height ? view_width : view_height);
        handwriting_roi_x = handwriting_clamp_int(handwriting_roi_x, 0, view_width - handwriting_roi_size);
        handwriting_roi_y = handwriting_clamp_int(handwriting_roi_y, 0, view_height - handwriting_roi_size);
        *roi_x = handwriting_roi_x;
        *roi_y = handwriting_roi_y;
        *roi_size = handwriting_roi_size;
    }
}

static void handwriting_view_roi_to_input_28x28(uint16_t const *frame,
                                                int width,
                                                int height,
                                                uint16_t rotation,
                                                int roi_x,
                                                int roi_y,
                                                int roi_size,
                                                int8_t *input)
{
    int8_t background = handwriting_model_background_value();

    for (int i = 0; i < HANDWRITING_MODEL_INPUT_SIZE; i++)
    {
        input[i] = background;
    }

    for (int y = 0; y < HANDWRITING_DIGIT_CANVAS; y++)
    {
        int view_y = roi_y + (((y * roi_size) + (roi_size / 2)) / HANDWRITING_DIGIT_CANVAS);

        for (int x = 0; x < HANDWRITING_DIGIT_CANVAS; x++)
        {
            int view_x = roi_x + (((x * roi_size) + (roi_size / 2)) / HANDWRITING_DIGIT_CANVAS);
            uint8_t gray = handwriting_rgb565_to_gray(*handwriting_rotated_view_pixel(frame,
                                                                                      width,
                                                                                      height,
                                                                                      view_x,
                                                                                      view_y,
                                                                                      rotation));
            input[((y + HANDWRITING_DIGIT_OFFSET) * 28) + x + HANDWRITING_DIGIT_OFFSET] =
                handwriting_gray_to_model_input(gray);
        }
    }
}

static void handwriting_rgb565_to_input_28x28(uint16_t const *frame,
                                              int width,
                                              int height,
                                              uint16_t rotation,
                                              int8_t *input)
{
    int view_width = ((rotation == 90U) || (rotation == 270U)) ? height : width;
    int view_height = ((rotation == 90U) || (rotation == 270U)) ? width : height;
    int crop = (view_width < view_height) ? view_width : view_height;
    int roi_x = (view_width - crop) / 2;
    int roi_y = (view_height - crop) / 2;
    int roi_size = crop;
    rt_bool_t found;

    found = handwriting_find_digit_roi(frame, width, height, rotation, &roi_x, &roi_y, &roi_size);
    handwriting_smooth_roi(view_width, view_height, rotation, found, &roi_x, &roi_y, &roi_size);

    if ((found != RT_TRUE) && (handwriting_roi_valid != RT_TRUE))
    {
        roi_size = (crop * 3) / 4;
        if (roi_size <= 0)
        {
            roi_size = crop;
        }
        roi_x = (view_width - roi_size) / 2;
        roi_y = (view_height - roi_size) / 2;
    }

    handwriting_view_roi_to_input_28x28(frame, width, height, rotation, roi_x, roi_y, roi_size, input);
}

static void handwriting_fill_rect(uint16_t *frame, int width, int height,
                                  int x, int y, int rect_w, int rect_h, uint16_t color)
{
    if ((frame == RT_NULL) || (width <= 0) || (height <= 0))
    {
        return;
    }

    for (int py = y; py < (y + rect_h); py++)
    {
        if ((py < 0) || (py >= height))
        {
            continue;
        }

        uint16_t *row = frame + ((rt_size_t)py * (rt_size_t)width);
        for (int px = x; px < (x + rect_w); px++)
        {
            if ((px >= 0) && (px < width))
            {
                row[px] = color;
            }
        }
    }
}

static void handwriting_draw_digit(uint16_t *frame, int width, int height, uint8_t digit)
{
    int scale = 4;
    int pad = 4;
    int bg_w;
    int bg_h;

    if ((frame == RT_NULL) || (width <= 0) || (height <= 0) || (digit > 9U))
    {
        return;
    }

    if ((width >= 1280) || (height >= 720))
    {
        scale = 8;
        pad = 8;
    }
    else if ((width >= 640) || (height >= 480))
    {
        scale = 6;
        pad = 6;
    }

    bg_w = (5 * scale) + (2 * pad);
    bg_h = (7 * scale) + (2 * pad);
    handwriting_fill_rect(frame, width, height, 0, 0, bg_w, bg_h, HANDWRITING_OVERLAY_BG_RGB565);

    for (int row = 0; row < 7; row++)
    {
        uint8_t bits = handwriting_digit_font[digit][row];

        for (int col = 0; col < 5; col++)
        {
            if ((bits & (uint8_t)(1U << (4 - col))) != 0U)
            {
                handwriting_fill_rect(frame, width, height,
                                      pad + (col * scale),
                                      pad + (row * scale),
                                      scale,
                                      scale,
                                      HANDWRITING_OVERLAY_FG_RGB565);
            }
        }
    }

    handwriting_cache_clean_frame_region(frame, width, 0, 0, bg_w - 1, bg_h - 1);
}

static void handwriting_draw_digit_rotated(uint16_t *frame, int width, int height, uint8_t digit, uint16_t rotation)
{
    int scale = 4;
    int pad = 4;
    int bg_w;
    int bg_h;
    int view_width = ((rotation == 90U) || (rotation == 270U)) ? height : width;
    int view_height = ((rotation == 90U) || (rotation == 270U)) ? width : height;
    int dirty_x1 = width;
    int dirty_y1 = height;
    int dirty_x2 = -1;
    int dirty_y2 = -1;

    if ((frame == RT_NULL) || (width <= 0) || (height <= 0) || (digit > 9U))
    {
        return;
    }

    if ((view_width >= 1280) || (view_height >= 720))
    {
        scale = 8;
        pad = 8;
    }
    else if ((view_width >= 640) || (view_height >= 480))
    {
        scale = 6;
        pad = 6;
    }

    bg_w = (5 * scale) + (2 * pad);
    bg_h = (7 * scale) + (2 * pad);

    for (int view_y = 0; view_y < bg_h; view_y++)
    {
        for (int view_x = 0; view_x < bg_w; view_x++)
        {
            uint16_t *pixel = (uint16_t *)handwriting_rotated_view_pixel(frame,
                                                                         width,
                                                                         height,
                                                                         view_x,
                                                                         view_y,
                                                                         rotation);
            int src_index = (int)(pixel - frame);
            int src_x = src_index % width;
            int src_y = src_index / width;

            *pixel = HANDWRITING_OVERLAY_BG_RGB565;
            if (src_x < dirty_x1)
            {
                dirty_x1 = src_x;
            }
            if (src_y < dirty_y1)
            {
                dirty_y1 = src_y;
            }
            if (src_x > dirty_x2)
            {
                dirty_x2 = src_x;
            }
            if (src_y > dirty_y2)
            {
                dirty_y2 = src_y;
            }
        }
    }

    for (int row = 0; row < 7; row++)
    {
        uint8_t bits = handwriting_digit_font[digit][row];

        for (int col = 0; col < 5; col++)
        {
            if ((bits & (uint8_t)(1U << (4 - col))) == 0U)
            {
                continue;
            }

            for (int dy = 0; dy < scale; dy++)
            {
                for (int dx = 0; dx < scale; dx++)
                {
                    int view_x = pad + (col * scale) + dx;
                    int view_y = pad + (row * scale) + dy;
                    uint16_t *pixel = (uint16_t *)handwriting_rotated_view_pixel(frame,
                                                                                 width,
                                                                                 height,
                                                                                 view_x,
                                                                                 view_y,
                                                                                 rotation);
                    *pixel = HANDWRITING_OVERLAY_FG_RGB565;
                }
            }
        }
    }

    handwriting_cache_clean_frame_region(frame, width, dirty_x1, dirty_y1, dirty_x2, dirty_y2);
}

static void handwriting_digit_overlay_size(int width, int height, uint16_t rotation,
                                           int *bg_w, int *bg_h)
{
    int view_width = ((rotation == 90U) || (rotation == 270U)) ? height : width;
    int view_height = ((rotation == 90U) || (rotation == 270U)) ? width : height;
    int scale = 4;
    int pad = 4;

    if ((view_width >= 1280) || (view_height >= 720))
    {
        scale = 8;
        pad = 8;
    }
    else if ((view_width >= 640) || (view_height >= 480))
    {
        scale = 6;
        pad = 6;
    }

    *bg_w = (5 * scale) + (2 * pad);
    *bg_h = (7 * scale) + (2 * pad);
}

static void handwriting_draw_input_preview(uint16_t *frame,
                                           int width,
                                           int height,
                                           uint16_t rotation,
                                           int8_t const *input)
{
#if HANDWRITING_SHOW_INPUT_PREVIEW
    int digit_bg_w;
    int digit_bg_h;
    int preview_x;
    int preview_y;
    int preview_size = 28 * HANDWRITING_INPUT_PREVIEW_SCALE;
    int dirty_x1 = width;
    int dirty_y1 = height;
    int dirty_x2 = -1;
    int dirty_y2 = -1;

    if ((frame == RT_NULL) || (input == RT_NULL) || (width <= 0) || (height <= 0))
    {
        return;
    }

    handwriting_digit_overlay_size(width, height, rotation, &digit_bg_w, &digit_bg_h);
    preview_x = 0;
    preview_y = digit_bg_h + HANDWRITING_INPUT_PREVIEW_GAP;

    for (int y = 0; y < preview_size; y++)
    {
        int input_y = y / HANDWRITING_INPUT_PREVIEW_SCALE;

        for (int x = 0; x < preview_size; x++)
        {
            int input_x = x / HANDWRITING_INPUT_PREVIEW_SCALE;
            int view_x = preview_x + x;
            int view_y = preview_y + y;
            uint16_t *pixel = (uint16_t *)handwriting_rotated_view_pixel(frame,
                                                                         width,
                                                                         height,
                                                                         view_x,
                                                                         view_y,
                                                                         rotation);
            int src_index = (int)(pixel - frame);
            int src_x = src_index % width;
            int src_y = src_index / width;

            *pixel = handwriting_gray_to_rgb565(handwriting_model_input_to_gray(input[(input_y * 28) + input_x]));

            if (src_x < dirty_x1)
            {
                dirty_x1 = src_x;
            }
            if (src_y < dirty_y1)
            {
                dirty_y1 = src_y;
            }
            if (src_x > dirty_x2)
            {
                dirty_x2 = src_x;
            }
            if (src_y > dirty_y2)
            {
                dirty_y2 = src_y;
            }
        }
    }

    handwriting_cache_clean_frame_region(frame, width, dirty_x1, dirty_y1, dirty_x2, dirty_y2);
#else
    RT_UNUSED(frame);
    RT_UNUSED(width);
    RT_UNUSED(height);
    RT_UNUSED(rotation);
    RT_UNUSED(input);
#endif
}

int handwriting_model_init(void)
{
    int16_t status;

    status = RM_ETHOSU_Open(&g_rm_ethosu0_ctrl, &g_rm_ethosu0_cfg);
    if (status != FSP_SUCCESS)
    {
        return -RT_ERROR;
    }

    handwriting_model_ready = RT_TRUE;
    return RT_EOK;
}

int handwriting_model_run(int8_t const *input_28x28, int8_t *scores, uint8_t *digit)
{
    int8_t *model_input;
    int8_t *model_output;
    uint8_t best = 0U;

    if ((handwriting_model_ready != RT_TRUE) || (input_28x28 == RT_NULL))
    {
        return -RT_ERROR;
    }

    model_input = GetModelInputPtr_serving_default_image_0();
    memcpy(model_input, input_28x28, HANDWRITING_MODEL_INPUT_SIZE);
    handwriting_cache_clean(model_input, HANDWRITING_MODEL_INPUT_SIZE);

    if (RunModel(false) != 0)
    {
        return -RT_ERROR;
    }

    model_output = GetModelOutputPtr_StatefulPartitionedCall_0_70015();
    handwriting_cache_invalidate(model_output, HANDWRITING_MODEL_OUTPUT_SIZE);

    for (uint8_t i = 1U; i < HANDWRITING_MODEL_OUTPUT_SIZE; i++)
    {
        if (model_output[i] > model_output[best])
        {
            best = i;
        }
    }

    if (scores != RT_NULL)
    {
        memcpy(scores, model_output, HANDWRITING_MODEL_OUTPUT_SIZE);
    }

    if (digit != RT_NULL)
    {
        *digit = best;
    }

    return RT_EOK;
}

static uint8_t handwriting_vote_digit(uint8_t digit)
{
    uint8_t counts[10] = {0};
    uint8_t best = digit;

    handwriting_vote_history[handwriting_vote_index] = digit;
    handwriting_vote_index = (uint8_t)((handwriting_vote_index + 1U) % HANDWRITING_VOTE_HISTORY);
    if (handwriting_vote_count < HANDWRITING_VOTE_HISTORY)
    {
        handwriting_vote_count++;
    }

    for (uint8_t i = 0; i < handwriting_vote_count; i++)
    {
        if (handwriting_vote_history[i] <= 9U)
        {
            counts[handwriting_vote_history[i]]++;
        }
    }

    for (uint8_t i = 0; i < 10U; i++)
    {
        if (counts[i] > counts[best])
        {
            best = i;
        }
    }

    return best;
}

int handwriting_model_process_rgb565(uint16_t *frame, int width, int height, uint16_t rotation)
{
    uint8_t digit = 0U;

    if ((frame == RT_NULL) || (width <= 0) || (height <= 0))
    {
        return -RT_ERROR;
    }

    if ((rotation != 90U) && (rotation != 180U) && (rotation != 270U))
    {
        rotation = 0U;
    }

    handwriting_rgb565_to_input_28x28(frame, width, height, rotation, handwriting_input);

    if (handwriting_model_run(handwriting_input, RT_NULL, &digit) != RT_EOK)
    {
        return -RT_ERROR;
    }

    digit = handwriting_vote_digit(digit);

    if (rotation == 0U)
    {
        handwriting_draw_digit(frame, width, height, digit);
    }
    else
    {
        handwriting_draw_digit_rotated(frame, width, height, digit, rotation);
    }

    handwriting_draw_input_preview(frame, width, height, rotation, handwriting_input);

    return RT_EOK;
}

#endif
