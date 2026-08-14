#include "pointer_mapping.h"

#include <assert.h>
#include <stdio.h>

int main(void) {
    int x;
    int y;
    /* Retina 2x drawable with horizontal letterboxing. */
    assert(fe8_pointer_window_to_canvas(
        1800, 1097, 3600, 2194, 480, 320, 900, 548, &x, &y));
    assert(x == 240 && y == 159);
    assert(!fe8_pointer_window_to_canvas(
        1800, 1097, 3600, 2194, 480, 320, 20, 548, &x, &y));

    /* Same canvas location must be DPI-independent. */
    assert(fe8_pointer_window_to_canvas(
        960, 640, 1920, 1280, 480, 320, 336, 288, &x, &y));
    assert(x == 168 && y == 144);
    assert(fe8_pointer_window_to_canvas(
        960, 640, 960, 640, 480, 320, 336, 288, &x, &y));
    assert(x == 168 && y == 144);
    puts("pointer mapping tests passed");
    return 0;
}
