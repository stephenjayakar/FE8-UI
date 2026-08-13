#ifndef FE8_HOST_CURSOR_H
#define FE8_HOST_CURSOR_H

#include <stddef.h>
#include <stdint.h>

/* Draws a large FE-themed pointer whose tip is exactly at (hotspot_x,
 * hotspot_y). Pixels use the frontend's 0xAABBGGRR host-canvas format. */
void fe8_host_draw_mouse_cursor(uint32_t *pixels, size_t stride,
    int width, int height, int hotspot_x, int hotspot_y);

#endif
