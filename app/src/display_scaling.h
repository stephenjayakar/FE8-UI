#ifndef FE8_DISPLAY_SCALING_H
#define FE8_DISPLAY_SCALING_H

typedef struct Fe8DisplayScaling {
    int minimum_canvas_width;
    int minimum_canvas_height;
    int minimum_view_width;
    int minimum_view_height;
    int drawable_width;
    int drawable_height;
    double zoom_factor;
    double base_pixel_scale;
    double pixel_scale;
    int canvas_width;
    int canvas_height;
} Fe8DisplayScaling;

void fe8_display_scaling_init(Fe8DisplayScaling *scaling,
    int minimum_canvas_width, int minimum_canvas_height,
    int minimum_view_width, int minimum_view_height);
/* Returns nonzero when either the drawable geometry or logical canvas changes. */
int fe8_display_scaling_resize(Fe8DisplayScaling *scaling,
    int drawable_width, int drawable_height);
int fe8_display_scaling_adjust(Fe8DisplayScaling *scaling,
    double wheel_delta, double sensitivity);

#endif
