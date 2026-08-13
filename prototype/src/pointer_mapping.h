#ifndef FE8_POINTER_MAPPING_H
#define FE8_POINTER_MAPPING_H

int fe8_pointer_window_to_canvas(
    int window_width, int window_height,
    int drawable_width, int drawable_height,
    int canvas_width, int canvas_height,
    int window_x, int window_y,
    int *canvas_x, int *canvas_y);

#endif
