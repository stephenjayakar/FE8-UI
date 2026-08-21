#include "host_text.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int count_nonzero(const uint32_t *pixels, int count) {
    int nonzero = 0;
    int index;
    for (index = 0; index < count; ++index) {
        if (pixels[index]) ++nonzero;
    }
    return nonzero;
}

static int count_nonzero_rows(const uint32_t *pixels, int stride,
    int width, int y_begin, int y_end) {
    int count = 0;
    int x;
    int y;
    for (y = y_begin; y < y_end; ++y) {
        for (x = 0; x < width; ++x) {
            if (pixels[y * stride + x]) ++count;
        }
    }
    return count;
}

int main(void) {
    enum { WIDTH = 96, HEIGHT = 48 };
    Fe8HostTextCanvas canvas;
    uint32_t regular[WIDTH * HEIGHT];
    uint32_t semibold[WIDTH * HEIGHT];
    uint32_t wrapped[WIDTH * HEIGHT];
    int regular_count;
    int semibold_count;

    memset(&canvas, 0, sizeof(canvas));
    memset(regular, 0, sizeof(regular));
    assert(!fe8_host_text_begin(NULL, regular, WIDTH, WIDTH, HEIGHT));
    assert(!fe8_host_text_begin(&canvas, NULL, WIDTH, WIDTH, HEIGHT));
    assert(!fe8_host_text_begin(&canvas, regular, WIDTH - 1, WIDTH, HEIGHT));

    assert(fe8_host_text_begin(&canvas, regular, WIDTH, WIDTH, HEIGHT));
    fe8_host_text_draw(&canvas, 3, 4, 60, 12, "Aa 19", 8.0f,
        UINT32_C(0xFF332211), FE8_HOST_TEXT_REGULAR, 0);
    regular_count = count_nonzero(regular, WIDTH * HEIGHT);
    assert(regular_count > 0);
    assert(regular[0] == 0);
    assert(regular[70 + 3 * WIDTH] == 0);
    fe8_host_text_end(&canvas);
    assert(!canvas.context);

    memset(semibold, 0, sizeof(semibold));
    assert(fe8_host_text_begin(&canvas, semibold, WIDTH, WIDTH, HEIGHT));
    fe8_host_text_draw(&canvas, 3, 4, 60, 12, "Aa 19", 8.0f,
        UINT32_C(0xFF332211), FE8_HOST_TEXT_SEMIBOLD, 0);
    semibold_count = count_nonzero(semibold, WIDTH * HEIGHT);
    assert(semibold_count > regular_count);
    fe8_host_text_end(&canvas);

    memset(wrapped, 0, sizeof(wrapped));
    assert(fe8_host_text_begin(&canvas, wrapped, WIDTH, WIDTH, HEIGHT));
    fe8_host_text_draw(&canvas, 2, 2, 14, 36, "A A A A", 8.0f,
        UINT32_C(0xFFFFFFFF), FE8_HOST_TEXT_REGULAR, 1);
    assert(count_nonzero_rows(wrapped, WIDTH, WIDTH, 2, 10) > 0);
    assert(count_nonzero_rows(wrapped, WIDTH, WIDTH, 11, 38) > 0);
    fe8_host_text_draw(&canvas, 30, 2, 30, 12, "Eirika \xE2\x86\x92", 8.0f,
        UINT32_C(0x80FFFFFF), FE8_HOST_TEXT_REGULAR, 0);
    fe8_host_text_end(&canvas);

    puts("host text tests passed");
    return 0;
}
