#include "host_cursor.h"

#include <string.h>

enum { CURSOR_GRID = 24 };

static uint32_t cursor_color(char code) {
    /* Host canvas colors are 0xAABBGGRR. */
    switch (code) {
    case 'K': return UINT32_C(0xFF1F1018); /* ink outline */
    case 'W': return UINT32_C(0xFFEAC968); /* light FE blue */
    case 'B': return UINT32_C(0xFFA45220); /* royal-blue inlay */
    case 'G': return UINT32_C(0xFF48D3FF); /* gold trim */
    case 'R': return UINT32_C(0xFF4030BE); /* red seal */
    default: return 0;
    }
}

void fe8_host_draw_mouse_cursor(uint32_t *pixels, size_t stride,
    int width, int height, int hotspot_x, int hotspot_y) {
    /* A crisp tactician's pointer. The high-contrast silhouette remains
     * readable over forests, water, range overlays, and native FE8 menus. */
    static const char *const art[CURSOR_GRID] = {
        "K.......................",
        "KK......................",
        "KWK.....................",
        "KWGK....................",
        "KWWGK...................",
        "KWWBGK..................",
        "KWWWBGK.................",
        "KWWWWBGK................",
        "KWWWWWBGK...............",
        "KWWWWWWBGK..............",
        "KWWWWWWWBGK.............",
        "KWWWWWWWWBGK............",
        "KWWWWWWWWWRGK...........",
        "KWWWWWWKKKKKK...........",
        "KWWWWKGBBGK.............",
        "KWWWK.KGBBGK............",
        "KWWK...KGBBGK...........",
        "KWK.....KGBBGK..........",
        "KK.......KGBBGK.........",
        "K.........KGBGK.........",
        "...........KGGK.........",
        "............KK..........",
        "........................",
        "........................",
    };
    int y;
    if (!pixels || stride < (size_t)width || width <= 0 || height <= 0)
        return;
    for (y = 0; y < CURSOR_GRID; ++y) {
        int destination_y = hotspot_y + y;
        size_t row_width = strlen(art[y]);
        int x;
        if (destination_y < 0 || destination_y >= height)
            continue;
        for (x = 0; x < CURSOR_GRID && (size_t)x < row_width; ++x) {
            int destination_x = hotspot_x + x;
            uint32_t color = cursor_color(art[y][x]);
            if (!color || destination_x < 0 || destination_x >= width)
                continue;
            pixels[(size_t)destination_y * stride + destination_x] = color;
        }
    }
}
