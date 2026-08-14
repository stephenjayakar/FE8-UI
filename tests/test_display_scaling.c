#include "display_scaling.h"

#include <assert.h>
#include <stdio.h>

int main(void) {
    Fe8DisplayScaling scaling;
    fe8_display_scaling_init(&scaling, 480, 320, 240, 160);
    assert(fe8_display_scaling_resize(&scaling, 2458, 1536));
    assert(scaling.base_pixel_scale == 4.8);
    assert(scaling.pixel_scale == 4.8);
    assert(scaling.canvas_width == 513);
    assert(scaling.canvas_height == 320);

    assert(fe8_display_scaling_adjust(&scaling, 1.0, 0.005));
    assert(scaling.zoom_factor > 1.004 && scaling.zoom_factor < 1.006);
    assert(scaling.canvas_width == 510);
    assert(scaling.canvas_height == 319);
    assert(fe8_display_scaling_adjust(&scaling, 0.5, 0.005));
    assert(scaling.zoom_factor > 1.007 && scaling.zoom_factor < 1.008);
    assert(fe8_display_scaling_adjust(&scaling, -99.0, 0.005));
    assert(scaling.zoom_factor == 1.0);
    assert(scaling.canvas_width == 513);
    assert(scaling.canvas_height == 320);

    assert(fe8_display_scaling_adjust(&scaling, 1.0, 0.03));
    assert(scaling.zoom_factor > 1.029 && scaling.zoom_factor < 1.031);

    puts("display scaling tests passed");
    return 0;
}
