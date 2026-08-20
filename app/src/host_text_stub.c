#include "host_text.h"

int fe8_host_text_begin(Fe8HostTextCanvas *canvas, uint32_t *pixels,
    int stride, int width, int height) {
    (void)pixels; (void)stride; (void)width;
    if (!canvas) return 0;
    canvas->context = canvas;
    canvas->height = height;
    return 1;
}

void fe8_host_text_draw(Fe8HostTextCanvas *canvas, int x, int y,
    int width, int height, const char *text, float size, uint32_t color,
    Fe8HostTextWeight weight, int wrap) {
    (void)canvas; (void)x; (void)y; (void)width; (void)height;
    (void)text; (void)size; (void)color; (void)weight; (void)wrap;
}

void fe8_host_text_end(Fe8HostTextCanvas *canvas) {
    if (canvas) canvas->context = 0;
}
