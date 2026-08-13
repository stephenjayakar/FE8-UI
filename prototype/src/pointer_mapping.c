#include "pointer_mapping.h"

int fe8_pointer_window_to_canvas(
    int window_width, int window_height,
    int drawable_width, int drawable_height,
    int canvas_width, int canvas_height,
    int window_x, int window_y,
    int *canvas_x, int *canvas_y) {
    int draw_width;
    int draw_height;
    int pad_x;
    int pad_y;
    double drawable_x;
    double drawable_y;
    if (window_width <= 0 || window_height <= 0 ||
            drawable_width <= 0 || drawable_height <= 0 ||
            canvas_width <= 0 || canvas_height <= 0 ||
            !canvas_x || !canvas_y)
        return 0;

    draw_width = drawable_width;
    draw_height = drawable_height;
    if ((long long)draw_width * canvas_height >
            (long long)draw_height * canvas_width)
        draw_width = draw_height * canvas_width / canvas_height;
    else if ((long long)draw_width * canvas_height <
            (long long)draw_height * canvas_width)
        draw_height = draw_width * canvas_height / canvas_width;
    pad_x = (drawable_width - draw_width) / 2;
    pad_y = (drawable_height - draw_height) / 2;
    drawable_x = (double)window_x * drawable_width / window_width;
    drawable_y = (double)window_y * drawable_height / window_height;
    if (drawable_x < pad_x || drawable_y < pad_y ||
            drawable_x >= pad_x + draw_width ||
            drawable_y >= pad_y + draw_height)
        return 0;
    *canvas_x = (int)((drawable_x - pad_x) * canvas_width / draw_width);
    *canvas_y = (int)((drawable_y - pad_y) * canvas_height / draw_height);
    return *canvas_x >= 0 && *canvas_x < canvas_width &&
        *canvas_y >= 0 && *canvas_y < canvas_height;
}
