#ifndef FE8_HOST_TEXT_H
#define FE8_HOST_TEXT_H

#include <stdint.h>

typedef enum Fe8HostTextWeight {
    FE8_HOST_TEXT_REGULAR,
    FE8_HOST_TEXT_MEDIUM,
    FE8_HOST_TEXT_SEMIBOLD,
} Fe8HostTextWeight;

typedef struct Fe8HostTextCanvas {
    void *context;
    uint32_t *pixels;
    int stride;
    int width;
    int height;
} Fe8HostTextCanvas;

int fe8_host_text_begin(Fe8HostTextCanvas *canvas, uint32_t *pixels,
    int stride, int width, int height);
/* Colors and pixels are ABGR. Non-wrapping labels ellipsize to the box width. */
void fe8_host_text_draw(Fe8HostTextCanvas *canvas, int x, int y,
    int width, int height, const char *text, float size, uint32_t color,
    Fe8HostTextWeight weight, int wrap);
void fe8_host_text_end(Fe8HostTextCanvas *canvas);

#endif
