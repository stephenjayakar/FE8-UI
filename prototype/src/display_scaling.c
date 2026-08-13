#include "display_scaling.h"

#include <string.h>

static int minimum_int(int a, int b) {
    return a < b ? a : b;
}

static int calculate_layout(Fe8DisplayScaling *scaling) {
    int old_width = scaling->canvas_width;
    int old_height = scaling->canvas_height;
    double width_scale = (double)scaling->drawable_width /
        scaling->minimum_canvas_width;
    double height_scale = (double)scaling->drawable_height /
        scaling->minimum_canvas_height;
    double maximum_scale = minimum_int(
        scaling->drawable_width / scaling->minimum_view_width,
        scaling->drawable_height / scaling->minimum_view_height);
    double maximum_zoom;
    scaling->base_pixel_scale = width_scale < height_scale ? width_scale : height_scale;
    if (scaling->base_pixel_scale <= 0.0)
        scaling->base_pixel_scale = 1.0;
    if (maximum_scale < scaling->base_pixel_scale)
        maximum_scale = scaling->base_pixel_scale;
    maximum_zoom = maximum_scale / scaling->base_pixel_scale;
    if (scaling->zoom_factor < 1.0)
        scaling->zoom_factor = 1.0;
    if (scaling->zoom_factor > maximum_zoom)
        scaling->zoom_factor = maximum_zoom;
    scaling->pixel_scale = scaling->base_pixel_scale * scaling->zoom_factor;
    scaling->canvas_width = (int)(
        scaling->drawable_width / scaling->pixel_scale + 0.999999);
    scaling->canvas_height = (int)(
        scaling->drawable_height / scaling->pixel_scale + 0.999999);
    return scaling->canvas_width != old_width || scaling->canvas_height != old_height;
}

void fe8_display_scaling_init(Fe8DisplayScaling *scaling,
    int minimum_canvas_width, int minimum_canvas_height,
    int minimum_view_width, int minimum_view_height) {
    if (!scaling)
        return;
    memset(scaling, 0, sizeof(*scaling));
    scaling->minimum_canvas_width = minimum_canvas_width;
    scaling->minimum_canvas_height = minimum_canvas_height;
    scaling->minimum_view_width = minimum_view_width;
    scaling->minimum_view_height = minimum_view_height;
    scaling->zoom_factor = 1.0;
}

int fe8_display_scaling_resize(Fe8DisplayScaling *scaling,
    int drawable_width, int drawable_height) {
    if (!scaling || drawable_width <= 0 || drawable_height <= 0)
        return 0;
    scaling->drawable_width = drawable_width;
    scaling->drawable_height = drawable_height;
    return calculate_layout(scaling);
}

int fe8_display_scaling_adjust(Fe8DisplayScaling *scaling,
    double wheel_delta, double sensitivity) {
    double old_zoom;
    double factor = 1.0;
    double magnitude;
    if (!scaling || wheel_delta == 0.0 || sensitivity <= 0.0)
        return 0;
    old_zoom = scaling->zoom_factor;
    /* Avoid libm for this small exponent by multiplying once per
     * whole/fractional unit; SDL precise wheel deltas are normally below one. */
    magnitude = wheel_delta < 0.0 ? -wheel_delta : wheel_delta;
    while (magnitude >= 1.0) {
        factor *= 1.0 + sensitivity;
        magnitude -= 1.0;
    }
    factor *= 1.0 + sensitivity * magnitude;
    if (wheel_delta > 0.0)
        scaling->zoom_factor *= factor;
    else
        scaling->zoom_factor /= factor;
    calculate_layout(scaling);
    return scaling->zoom_factor != old_zoom;
}
