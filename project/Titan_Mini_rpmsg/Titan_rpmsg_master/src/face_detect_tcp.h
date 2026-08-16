#ifndef FACE_DETECT_TCP_H
#define FACE_DETECT_TCP_H

#include <stdint.h>

int face_detect_tcp_init(void);
int face_detect_tcp_process_rgb565(uint16_t *frame, int width, int height);

#endif
