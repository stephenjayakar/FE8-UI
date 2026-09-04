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

    double zoom = scaling.zoom_factor;
    int old_width = scaling.canvas_width, old_height = scaling.canvas_height;
    scaling.native_resolution = 1;
    assert(fe8_display_scaling_resize(&scaling, 2458, 1536));
    assert(scaling.canvas_width == 2458 && scaling.canvas_height == 1536);
    assert(scaling.pixel_scale == 1.0 && scaling.zoom_factor == zoom);
    assert(!fe8_display_scaling_adjust(&scaling, 10, 0.5));
    assert(scaling.zoom_factor == zoom);
    assert(fe8_display_scaling_resize(&scaling, 1920, 1080));
    assert(scaling.canvas_width == 1920 && scaling.canvas_height == 1080);
    assert(fe8_display_scaling_resize(&scaling, 2458, 1536));
    scaling.native_resolution = 0;
    assert(fe8_display_scaling_resize(&scaling, 2458, 1536));
    assert(scaling.canvas_width == old_width && scaling.canvas_height == old_height);
    assert(scaling.zoom_factor == zoom);
    puts("display scaling tests passed");
    return 0;
}
