/*
 * Minimal TCP camera sender for RGB565 frames.
 */

#include <rtthread.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netdb.h>

#include "camera_layer.h"
#include "tcm_sections.h"
#include "ov5640_tuning.h"

#define CAMERA_TCP_WIDTH              CAMERA_OV5640_RUNTIME_MAX_WIDTH
#define CAMERA_TCP_HEIGHT             CAMERA_OV5640_RUNTIME_MAX_HEIGHT
#define CAMERA_TCP_DEFAULT_WIDTH      640U
#define CAMERA_TCP_DEFAULT_HEIGHT     480U
#define CAMERA_TCP_DEFAULT_CHUNK_SIZE (32U * 1024U)
#define CAMERA_TCP_DEFAULT_PORT       9000
#define CAMERA_TCP_THREAD_STACK_SIZE  8192
#define CAMERA_TCP_THREAD_PRIORITY    7
#define CAMERA_TCP_STAGE_COUNT        1U
#define CAMERA_TCP_SCALE_DROP_FRAMES  2U
#define CAMERA_TCP_SCALE_WIDTH_ALIGN  16U

typedef struct camera_tcp_header
{
    char magic[4];
    rt_uint32_t sequence;
    rt_uint32_t length;
    rt_uint16_t width;
    rt_uint16_t height;
} camera_tcp_header_t;

typedef struct camera_tcp_control
{
    char magic[4];
    rt_uint16_t width;
    rt_uint16_t height;
} camera_tcp_control_t;

typedef struct camera_tcp_ev_control
{
    char magic[4];
    int16_t ev;
    rt_uint16_t reserved;
} camera_tcp_ev_control_t;

typedef struct camera_tcp_i16_control
{
    char magic[4];
    int16_t value;
    rt_uint16_t reserved;
} camera_tcp_i16_control_t;

typedef struct camera_tcp_u16_control
{
    char magic[4];
    rt_uint16_t value;
    rt_uint16_t reserved;
} camera_tcp_u16_control_t;

typedef struct camera_tcp_u32_control
{
    char magic[4];
    rt_uint32_t value;
} camera_tcp_u32_control_t;


static rt_thread_t camera_tcp_thread = RT_NULL;
static rt_sem_t camera_tcp_sem = RT_NULL;
static rt_mutex_t camera_tcp_mutex = RT_NULL;
static rt_bool_t camera_tcp_running = RT_FALSE;
static rt_bool_t camera_tcp_connected = RT_FALSE;
static rt_bool_t camera_tcp_reconnect_pending = RT_FALSE;
static char camera_tcp_host[32];
static rt_uint16_t camera_tcp_port = CAMERA_TCP_DEFAULT_PORT;

typedef struct camera_tcp_stage
{
    rt_uint8_t const *direct_src;
    rt_uint32_t sequence;
    rt_uint32_t length;
    rt_uint16_t width;
    rt_uint16_t height;
    char magic[4];
    rt_bool_t capture_paused;
} camera_tcp_stage_t;

static camera_tcp_stage_t camera_tcp_stage[CAMERA_TCP_STAGE_COUNT];
static rt_uint8_t camera_tcp_queue_head = 0;
static rt_uint8_t camera_tcp_queue_tail = 0;
static rt_uint8_t camera_tcp_queue_count = 0;
static rt_bool_t camera_tcp_queue_sending = RT_FALSE;
static camera_tcp_stage_t *camera_tcp_sending_stage = RT_NULL;

static rt_uint16_t camera_tcp_roi_width = CAMERA_TCP_DEFAULT_WIDTH;
static rt_uint16_t camera_tcp_roi_height = CAMERA_TCP_DEFAULT_HEIGHT;
static rt_uint16_t camera_tcp_roi_x = 0;
static rt_uint16_t camera_tcp_roi_y = 0;
static rt_uint16_t camera_tcp_view_rotation = 0;
static rt_bool_t camera_tcp_scale_pending = RT_FALSE;
static rt_uint16_t camera_tcp_pending_width = CAMERA_TCP_DEFAULT_WIDTH;
static rt_uint16_t camera_tcp_pending_height = CAMERA_TCP_DEFAULT_HEIGHT;
static rt_uint32_t camera_tcp_scale_drop_frames = 0;


static rt_bool_t camera_tcp_ensure_ipc(void)
{
    if (camera_tcp_sem == RT_NULL)
    {
        camera_tcp_sem = rt_sem_create("camtcp", 0, RT_IPC_FLAG_FIFO);
    }

    if (camera_tcp_mutex == RT_NULL)
    {
        camera_tcp_mutex = rt_mutex_create("camtcp", RT_IPC_FLAG_PRIO);
    }

    return (camera_tcp_sem != RT_NULL) && (camera_tcp_mutex != RT_NULL);
}

static fsp_err_t camera_tcp_apply_scale_locked(rt_uint16_t width, rt_uint16_t height)
{
    fsp_err_t err;

    err = camera_capture_scale_set(width, height);
    if (err != FSP_SUCCESS)
    {
        return err;
    }

    camera_tcp_roi_width = width;
    camera_tcp_roi_height = height;
    camera_tcp_roi_x = 0;
    camera_tcp_roi_y = 0;
    camera_tcp_scale_drop_frames = CAMERA_TCP_SCALE_DROP_FRAMES;

    return FSP_SUCCESS;
}

static rt_bool_t camera_tcp_drop_frames_locked(void)
{
    rt_bool_t resume_capture = RT_FALSE;

    for (rt_uint8_t i = 0; i < CAMERA_TCP_STAGE_COUNT; i++)
    {
        if (camera_tcp_stage[i].capture_paused == RT_TRUE)
        {
            resume_capture = RT_TRUE;
        }
        camera_tcp_stage[i].capture_paused = RT_FALSE;
        camera_tcp_stage[i].direct_src = RT_NULL;
        camera_tcp_stage[i].sequence = 0;
        camera_tcp_stage[i].length = 0;
        camera_tcp_stage[i].width = 0;
        camera_tcp_stage[i].height = 0;
        camera_tcp_stage[i].magic[0] = '\0';
        camera_tcp_stage[i].magic[1] = '\0';
        camera_tcp_stage[i].magic[2] = '\0';
        camera_tcp_stage[i].magic[3] = '\0';
    }

    camera_tcp_queue_head = 0;
    camera_tcp_queue_tail = 0;
    camera_tcp_queue_count = 0;
    camera_tcp_queue_sending = RT_FALSE;
    camera_tcp_sending_stage = RT_NULL;

    while ((camera_tcp_sem != RT_NULL) && (rt_sem_take(camera_tcp_sem, 0) == RT_EOK))
    {
    }

    return resume_capture;
}

static rt_bool_t camera_tcp_scale_valid(rt_uint32_t width, rt_uint32_t height)
{
    return (width > 0U) && (height > 0U) &&
           (width <= CAMERA_TCP_WIDTH) && (height <= CAMERA_TCP_HEIGHT) &&
           ((width % CAMERA_TCP_SCALE_WIDTH_ALIGN) == 0U) && ((height & 1U) == 0U);
}

static rt_uint16_t camera_tcp_normalize_rotation(rt_uint16_t rotation)
{
    switch (rotation)
    {
    case 90U:
    case 180U:
    case 270U:
        return rotation;
    default:
        return 0U;
    }
}

rt_uint16_t camera_tcp_rotation_get(void)
{
    rt_uint16_t rotation;

    if ((camera_tcp_mutex == RT_NULL) ||
        (rt_mutex_take(camera_tcp_mutex, rt_tick_from_millisecond(2)) != RT_EOK))
    {
        return 0U;
    }

    rotation = camera_tcp_view_rotation;
    rt_mutex_release(camera_tcp_mutex);
    return rotation;
}

static void camera_tcp_request_rotation(rt_uint16_t rotation)
{
    if (camera_tcp_mutex == RT_NULL)
    {
        return;
    }

    if (rt_mutex_take(camera_tcp_mutex, rt_tick_from_millisecond(10)) != RT_EOK)
    {
        return;
    }

    camera_tcp_view_rotation = camera_tcp_normalize_rotation(rotation);
    rt_mutex_release(camera_tcp_mutex);
}

static void camera_tcp_reset_stream_state(void)
{
    rt_bool_t resume_capture = RT_FALSE;

    if (camera_tcp_mutex == RT_NULL)
    {
        return;
    }

    if (rt_mutex_take(camera_tcp_mutex, rt_tick_from_millisecond(100)) != RT_EOK)
    {
        return;
    }

    resume_capture = camera_tcp_drop_frames_locked();
    camera_tcp_scale_pending = RT_FALSE;
    camera_tcp_pending_width = CAMERA_TCP_DEFAULT_WIDTH;
    camera_tcp_pending_height = CAMERA_TCP_DEFAULT_HEIGHT;
    if (camera_tcp_scale_valid(camera_tcp_pending_width, camera_tcp_pending_height) == RT_TRUE)
    {
        (void)camera_tcp_apply_scale_locked(camera_tcp_pending_width, camera_tcp_pending_height);
    }
    else
    {
        camera_tcp_pending_width = camera_capture_output_width_get();
        camera_tcp_pending_height = camera_capture_output_height_get();
        camera_tcp_roi_width = camera_tcp_pending_width;
        camera_tcp_roi_height = camera_tcp_pending_height;
        camera_tcp_roi_x = 0;
        camera_tcp_roi_y = 0;
        camera_tcp_scale_drop_frames = CAMERA_TCP_SCALE_DROP_FRAMES;
    }

    rt_mutex_release(camera_tcp_mutex);

    if (resume_capture == RT_TRUE)
    {
        camera_capture_stream_resume_vin();
    }
}


static void camera_tcp_request_scale(rt_uint16_t width, rt_uint16_t height)
{
    rt_bool_t resume_capture = RT_FALSE;

    if ((camera_tcp_mutex == RT_NULL) || (camera_tcp_scale_valid(width, height) != RT_TRUE))
    {
        return;
    }

    if (rt_mutex_take(camera_tcp_mutex, rt_tick_from_millisecond(10)) != RT_EOK)
    {
        return;
    }

    resume_capture = camera_tcp_drop_frames_locked();
    camera_tcp_scale_pending = RT_FALSE;
    camera_tcp_pending_width = width;
    camera_tcp_pending_height = height;

    if (camera_tcp_apply_scale_locked(width, height) != FSP_SUCCESS)
    {
    }

    rt_mutex_release(camera_tcp_mutex);

    if (resume_capture == RT_TRUE)
    {
        camera_capture_stream_resume_vin();
    }
}

static void camera_tcp_poll_control(int fd)
{
    camera_tcp_control_t ctrl;
    camera_tcp_ev_control_t ev_ctrl;
    camera_tcp_i16_control_t i16_ctrl;
    camera_tcp_u16_control_t u16_ctrl;
    camera_tcp_u32_control_t u32_ctrl;
    char magic[4];
    int received;

    received = recv(fd, magic, sizeof(magic), MSG_DONTWAIT | MSG_PEEK);
    if (received < (int)sizeof(magic))
    {
        return;
    }

    if ((magic[0] == 'S') && (magic[1] == 'C') &&
        (magic[2] == 'L') && (magic[3] == '0'))
    {
        received = recv(fd, &ctrl, sizeof(ctrl), MSG_DONTWAIT | MSG_PEEK);
        if (received < (int)sizeof(ctrl))
        {
            return;
        }

        received = recv(fd, &ctrl, sizeof(ctrl), MSG_DONTWAIT);
        if (received == (int)sizeof(ctrl))
        {
            camera_tcp_request_scale(ctrl.width, ctrl.height);
        }
        return;
    }

    if ((magic[0] == 'A') && (magic[1] == 'E') &&
        (magic[2] == 'V') && (magic[3] == '0'))
    {
        received = recv(fd, &ev_ctrl, sizeof(ev_ctrl), MSG_DONTWAIT | MSG_PEEK);
        if (received < (int)sizeof(ev_ctrl))
        {
            return;
        }

        received = recv(fd, &ev_ctrl, sizeof(ev_ctrl), MSG_DONTWAIT);
        if (received == (int)sizeof(ev_ctrl))
        {
            (void)ov5640_tuning_apply_ev((int)ev_ctrl.ev);
        }
        return;
    }

    if ((magic[0] == 'A') && (magic[1] == 'E') &&
        (magic[2] == 'G') && (magic[3] == '0'))
    {
        received = recv(fd, &i16_ctrl, sizeof(i16_ctrl), MSG_DONTWAIT | MSG_PEEK);
        if (received < (int)sizeof(i16_ctrl))
        {
            return;
        }

        received = recv(fd, &i16_ctrl, sizeof(i16_ctrl), MSG_DONTWAIT);
        if (received == (int)sizeof(i16_ctrl))
        {
            (void)ov5640_tuning_set_ae_ag((i16_ctrl.value != 0) ? RT_TRUE : RT_FALSE);
        }
        return;
    }

    if ((magic[0] == 'S') && (magic[1] == 'H') &&
        (magic[2] == 'T') && (magic[3] == '0'))
    {
        received = recv(fd, &u32_ctrl, sizeof(u32_ctrl), MSG_DONTWAIT | MSG_PEEK);
        if (received < (int)sizeof(u32_ctrl))
        {
            return;
        }

        received = recv(fd, &u32_ctrl, sizeof(u32_ctrl), MSG_DONTWAIT);
        if (received == (int)sizeof(u32_ctrl))
        {
            (void)ov5640_tuning_set_shutter(u32_ctrl.value);
        }
        return;
    }

    if ((magic[0] == 'A') && (magic[1] == 'G') &&
        (magic[2] == 'N') && (magic[3] == '0'))
    {
        received = recv(fd, &u16_ctrl, sizeof(u16_ctrl), MSG_DONTWAIT | MSG_PEEK);
        if (received < (int)sizeof(u16_ctrl))
        {
            return;
        }

        received = recv(fd, &u16_ctrl, sizeof(u16_ctrl), MSG_DONTWAIT);
        if (received == (int)sizeof(u16_ctrl))
        {
            (void)ov5640_tuning_set_gain16(u16_ctrl.value);
        }
        return;
    }

    if ((magic[0] == 'R') && (magic[1] == 'O') &&
        (magic[2] == 'T') && (magic[3] == '0'))
    {
        received = recv(fd, &u16_ctrl, sizeof(u16_ctrl), MSG_DONTWAIT | MSG_PEEK);
        if (received < (int)sizeof(u16_ctrl))
        {
            return;
        }

        received = recv(fd, &u16_ctrl, sizeof(u16_ctrl), MSG_DONTWAIT);
        if (received == (int)sizeof(u16_ctrl))
        {
            camera_tcp_request_rotation(u16_ctrl.value);
        }
        return;
    }

    if ((magic[0] == 'A') && (magic[1] == 'E') &&
        (magic[2] == 'T') && (magic[3] == '0'))
    {
        received = recv(fd, &u16_ctrl, sizeof(u16_ctrl), MSG_DONTWAIT | MSG_PEEK);
        if (received < (int)sizeof(u16_ctrl))
        {
            return;
        }

        received = recv(fd, &u16_ctrl, sizeof(u16_ctrl), MSG_DONTWAIT);
        if (received == (int)sizeof(u16_ctrl))
        {
            (void)ov5640_tuning_set_ae_target((rt_uint8_t)u16_ctrl.value);
        }
        return;
    }

    if ((magic[0] == 'B') && (magic[1] == 'N') &&
        (magic[2] == 'D') && (magic[3] == '0'))
    {
        received = recv(fd, &i16_ctrl, sizeof(i16_ctrl), MSG_DONTWAIT | MSG_PEEK);
        if (received < (int)sizeof(i16_ctrl))
        {
            return;
        }

        received = recv(fd, &i16_ctrl, sizeof(i16_ctrl), MSG_DONTWAIT);
        if (received == (int)sizeof(i16_ctrl))
        {
            (void)ov5640_tuning_apply_banding((int)i16_ctrl.value);
        }
        return;
    }

    received = recv(fd, magic, sizeof(magic), MSG_DONTWAIT);
    if ((received == (int)sizeof(magic)) &&
        (magic[0] == 'A') && (magic[1] == 'F') &&
        (magic[2] == '0') && (magic[3] == '0'))
    {
        rt_kprintf("[camtcp] AF00 focus command received\n");
        ov5640_focus_trigger();
    }
}

static TCM_ITCM_CODE rt_bool_t camera_tcp_write_all(int fd, rt_uint8_t const *data, rt_size_t size)
{
    while (size > 0U)
    {
        rt_size_t chunk = size;
        int written;

        if (chunk > CAMERA_TCP_DEFAULT_CHUNK_SIZE)
        {
            chunk = CAMERA_TCP_DEFAULT_CHUNK_SIZE;
        }

        written = send(fd, data, chunk, 0);
        if (written <= 0)
        {
            return RT_FALSE;
        }

        data += written;
        size -= (rt_size_t)written;
    }

    return RT_TRUE;
}

static int camera_tcp_connect(void)
{
    int fd;
    int opt = 1;
    struct timeval timeout;
    struct sockaddr_in addr;
    char host[sizeof(camera_tcp_host)];
    rt_uint16_t port;

    host[0] = '\0';
    port = 0;
    if ((camera_tcp_mutex == RT_NULL) ||
        (rt_mutex_take(camera_tcp_mutex, rt_tick_from_millisecond(100)) != RT_EOK))
    {
        return -1;
    }

    rt_strncpy(host, camera_tcp_host, sizeof(host) - 1U);
    host[sizeof(host) - 1U] = '\0';
    port = camera_tcp_port;
    camera_tcp_reconnect_pending = RT_FALSE;
    rt_mutex_release(camera_tcp_mutex);

    if ((host[0] == '\0') || (port == 0U))
    {
        return -1;
    }

    fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0)
    {
        return -1;
    }

    timeout.tv_sec = 3;
    timeout.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

    rt_memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(host);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        closesocket(fd);
        return -1;
    }

    return fd;
}

void camera_tcp_frame_publish(uint8_t const *src, rt_uint32_t sequence)
{
    camera_tcp_stage_t *stage;
    rt_uint16_t width;
    rt_uint16_t height;
    rt_uint32_t payload_size;

    if ((src == RT_NULL) || (camera_tcp_running != RT_TRUE) || (camera_tcp_mutex == RT_NULL))
    {
        return;
    }

    if (rt_mutex_take(camera_tcp_mutex, rt_tick_from_millisecond(2)) != RT_EOK)
    {
        return;
    }

    if (camera_tcp_connected != RT_TRUE)
    {
        rt_mutex_release(camera_tcp_mutex);
        return;
    }

    if ((camera_tcp_queue_count > 0U) || (camera_tcp_queue_sending == RT_TRUE))
    {
        rt_mutex_release(camera_tcp_mutex);
        return;
    }

    if (camera_tcp_scale_drop_frames > 0U)
    {
        camera_tcp_scale_drop_frames--;
        rt_mutex_release(camera_tcp_mutex);
        return;
    }

    width = camera_tcp_roi_width;
    height = camera_tcp_roi_height;
    payload_size = (rt_uint32_t)width * height * 2U;
    if (payload_size > camera_capture_image_rgb565_size)
    {
        rt_mutex_release(camera_tcp_mutex);
        return;
    }

    rt_memcpy(camera_capture_image_rgb565, src, payload_size);

    stage = &camera_tcp_stage[camera_tcp_queue_tail];
    stage->capture_paused = RT_FALSE;
    stage->direct_src = camera_capture_image_rgb565;
    stage->sequence = sequence;
    stage->length = payload_size;
    stage->width = width;
    stage->height = height;
    stage->magic[0] = 'R';
    stage->magic[1] = 'G';
    stage->magic[2] = 'B';
    stage->magic[3] = '5';
    camera_tcp_queue_tail = (rt_uint8_t)((camera_tcp_queue_tail + 1U) % CAMERA_TCP_STAGE_COUNT);
    camera_tcp_queue_count++;

    rt_mutex_release(camera_tcp_mutex);
    rt_sem_release(camera_tcp_sem);
}

static camera_tcp_stage_t *camera_tcp_take_frame(void)
{
    camera_tcp_stage_t *stage;

    if (rt_mutex_take(camera_tcp_mutex, rt_tick_from_millisecond(100)) != RT_EOK)
    {
        return RT_NULL;
    }

    if ((camera_tcp_queue_count == 0U) || (camera_tcp_queue_sending == RT_TRUE))
    {
        rt_mutex_release(camera_tcp_mutex);
        return RT_NULL;
    }

    stage = &camera_tcp_stage[camera_tcp_queue_head];
    camera_tcp_queue_sending = RT_TRUE;
    camera_tcp_sending_stage = stage;

    rt_mutex_release(camera_tcp_mutex);
    return stage;
}

static void camera_tcp_release_frame(void)
{
    rt_bool_t apply_pending = RT_FALSE;
    rt_bool_t resume_capture = RT_FALSE;
    rt_uint16_t pending_width = 0;
    rt_uint16_t pending_height = 0;

    if (rt_mutex_take(camera_tcp_mutex, rt_tick_from_millisecond(100)) != RT_EOK)
    {
        return;
    }

    if (camera_tcp_queue_count > 0U)
    {
        camera_tcp_queue_head = (rt_uint8_t)((camera_tcp_queue_head + 1U) % CAMERA_TCP_STAGE_COUNT);
        camera_tcp_queue_count--;
    }
    if ((camera_tcp_sending_stage != RT_NULL) && (camera_tcp_sending_stage->capture_paused == RT_TRUE))
    {
        camera_tcp_sending_stage->capture_paused = RT_FALSE;
        resume_capture = RT_TRUE;
    }
    camera_tcp_sending_stage = RT_NULL;
    camera_tcp_queue_sending = RT_FALSE;

    if (camera_tcp_scale_pending == RT_TRUE)
    {
        apply_pending = RT_TRUE;
        pending_width = camera_tcp_pending_width;
        pending_height = camera_tcp_pending_height;
        camera_tcp_scale_pending = RT_FALSE;
    }

    if (apply_pending == RT_TRUE)
    {
        fsp_err_t err = camera_tcp_apply_scale_locked(pending_width, pending_height);

        RT_UNUSED(err);
    }

    if (resume_capture == RT_TRUE)
    {
        camera_capture_stream_resume_vin();
    }

    rt_mutex_release(camera_tcp_mutex);
}

static TCM_ITCM_CODE rt_bool_t camera_tcp_send_frame(int fd)
{
    camera_tcp_header_t header;
    camera_tcp_stage_t *stage;
    rt_uint8_t const *payload;
    rt_uint32_t payload_length;
    rt_bool_t ok;

    stage = camera_tcp_take_frame();
    if (stage == RT_NULL)
    {
        return RT_TRUE;
    }

    payload = stage->direct_src;
    payload_length = stage->length;

    header.sequence = stage->sequence;
    header.width = stage->width;
    header.height = stage->height;
    header.magic[0] = stage->magic[0];
    header.magic[1] = stage->magic[1];
    header.magic[2] = stage->magic[2];
    header.magic[3] = stage->magic[3];
    header.length = payload_length;

    ok = (camera_tcp_write_all(fd, (rt_uint8_t const *)&header, sizeof(header)) == RT_TRUE);
    if (ok == RT_TRUE)
    {
        ok = camera_tcp_write_all(fd, payload, payload_length);
    }

    camera_tcp_release_frame();
    return ok;
}

static void camera_tcp_thread_entry(void *parameter)
{
    RT_UNUSED(parameter);

    while (camera_tcp_running == RT_TRUE)
    {
        int fd = camera_tcp_connect();

        if (fd < 0)
        {
            camera_tcp_connected = RT_FALSE;
            rt_thread_mdelay(1000);
            continue;
        }

        camera_tcp_reset_stream_state();
        camera_tcp_connected = RT_TRUE;
        while (camera_tcp_running == RT_TRUE)
        {
            if (camera_tcp_reconnect_pending == RT_TRUE)
            {
                break;
            }
            camera_tcp_poll_control(fd);
            rt_sem_take(camera_tcp_sem, rt_tick_from_millisecond(100));
            camera_tcp_poll_control(fd);
            if (camera_tcp_send_frame(fd) != RT_TRUE)
            {
                break;
            }
        }

        camera_tcp_connected = RT_FALSE;
        camera_tcp_reset_stream_state();
        closesocket(fd);
        rt_thread_mdelay(100);
    }

    camera_tcp_connected = RT_FALSE;
    camera_tcp_thread = RT_NULL;
}

static rt_bool_t camera_tcp_start_worker(void)
{
    if (camera_tcp_thread != RT_NULL)
    {
        camera_tcp_running = RT_TRUE;
        return RT_TRUE;
    }

    if (camera_tcp_ensure_ipc() != RT_TRUE)
    {
        return RT_FALSE;
    }

    camera_tcp_running = RT_TRUE;
    camera_tcp_queue_head = 0;
    camera_tcp_queue_tail = 0;
    camera_tcp_queue_count = 0;
    camera_tcp_queue_sending = RT_FALSE;
    camera_tcp_sending_stage = RT_NULL;
    camera_tcp_scale_drop_frames = 0;
    camera_tcp_reconnect_pending = RT_FALSE;
    while (rt_sem_take(camera_tcp_sem, 0) == RT_EOK)
    {
    }
    camera_tcp_thread = rt_thread_create("camtcp",
                                         camera_tcp_thread_entry,
                                         RT_NULL,
                                         CAMERA_TCP_THREAD_STACK_SIZE,
                                         CAMERA_TCP_THREAD_PRIORITY,
                                         10);
    if (camera_tcp_thread != RT_NULL)
    {
        rt_thread_startup(camera_tcp_thread);
        return RT_TRUE;
    }

    camera_tcp_running = RT_FALSE;
    return RT_FALSE;
}

void camera_tcp_auto_start(void)
{
    if (camera_tcp_ensure_ipc() == RT_TRUE)
    {
        camera_tcp_request_scale(CAMERA_TCP_DEFAULT_WIDTH, CAMERA_TCP_DEFAULT_HEIGHT);
    }
    /* Command-based connection: use the "cam_connect" command to set the
     * PC server address and start connecting. */
}

static rt_bool_t camera_tcp_target_set(char const *host, rt_uint16_t port)
{
    rt_bool_t changed;

    if ((host == RT_NULL) || (host[0] == '\0') || (port == 0U) ||
        (camera_tcp_ensure_ipc() != RT_TRUE))
    {
        return RT_FALSE;
    }

    if (rt_mutex_take(camera_tcp_mutex, rt_tick_from_millisecond(100)) != RT_EOK)
    {
        return RT_FALSE;
    }

    changed = (rt_strncmp(camera_tcp_host, host, sizeof(camera_tcp_host)) != 0) ||
              (camera_tcp_port != port);
    rt_strncpy(camera_tcp_host, host, sizeof(camera_tcp_host) - 1U);
    camera_tcp_host[sizeof(camera_tcp_host) - 1U] = '\0';
    camera_tcp_port = port;

    if ((changed == RT_TRUE) && (camera_tcp_connected == RT_TRUE))
    {
        camera_tcp_reconnect_pending = RT_TRUE;
    }

    rt_mutex_release(camera_tcp_mutex);

    if ((changed == RT_TRUE) && (camera_tcp_sem != RT_NULL))
    {
        rt_sem_release(camera_tcp_sem);
    }

    return RT_TRUE;
}

static void camera_tcp_cmd_connect(int argc, char **argv)
{
    rt_uint16_t port = CAMERA_TCP_DEFAULT_PORT;

    if (argc < 2)
    {
        rt_kprintf("usage: cam_connect <ip> [port]\n");
        return;
    }
    if (argc >= 3)
    {
        port = (rt_uint16_t)atoi(argv[2]);
    }
    if (port == 0U)
    {
        rt_kprintf("cam_connect: invalid port\n");
        return;
    }
    if (camera_tcp_target_set(argv[1], port) != RT_TRUE)
    {
        rt_kprintf("cam_connect: set target failed\n");
        return;
    }
    if (camera_tcp_start_worker() != RT_TRUE)
    {
        rt_kprintf("cam_connect: start worker failed\n");
        return;
    }
    rt_kprintf("cam_connect: connecting to %s:%u\n", argv[1], port);
}
MSH_CMD_EXPORT_ALIAS(camera_tcp_cmd_connect, cam_connect, connect to camera TCP server);

static void camera_tcp_cmd_disconnect(int argc, char **argv)
{
    RT_UNUSED(argc);
    RT_UNUSED(argv);

    if (camera_tcp_mutex != RT_NULL)
    {
        rt_mutex_take(camera_tcp_mutex, rt_tick_from_millisecond(100));
        camera_tcp_host[0] = '\0';
        camera_tcp_reconnect_pending = RT_TRUE;
        rt_mutex_release(camera_tcp_mutex);
    }
    else
    {
        camera_tcp_reconnect_pending = RT_TRUE;
    }
    rt_kprintf("cam_disconnect: ok\n");
}
MSH_CMD_EXPORT_ALIAS(camera_tcp_cmd_disconnect, cam_disconnect, disconnect camera TCP server);

static void camera_tcp_cmd_status(int argc, char **argv)
{
    RT_UNUSED(argc);
    RT_UNUSED(argv);

    rt_kprintf("cam_status: target=%s:%u connected=%s running=%s\n",
               (camera_tcp_host[0] != '\0') ? camera_tcp_host : "(none)",
               camera_tcp_port,
               (camera_tcp_connected == RT_TRUE) ? "yes" : "no",
               (camera_tcp_running == RT_TRUE) ? "yes" : "no");
}
MSH_CMD_EXPORT_ALIAS(camera_tcp_cmd_status, cam_status, show camera TCP status);
