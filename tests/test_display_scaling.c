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

    {
        Fe8DisplayScaling settled;
        fe8_display_scaling_init(&settled, 480, 320, 240, 160);
        assert(fe8_display_scaling_resize(&settled, 1920, 1280));
        assert(settled.canvas_width == 480);
        assert(settled.canvas_height == 320);
        /* Maximization/HiDPI can change the drawable while preserving the
         * logical canvas. Backends must still be told to reapply their layer
         * geometry and callers must invalidate cached canvas coordinates. */
        assert(fe8_display_scaling_resize(&settled, 2400, 1600));
        assert(settled.canvas_width == 480);
        assert(settled.canvas_height == 320);
        assert(!fe8_display_scaling_resize(&settled, 2400, 1600));
    }

    puts("display scaling tests passed");
    return 0;
}
