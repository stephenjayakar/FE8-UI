#include "host_cursor.h"

#include <string.h>

enum { CURSOR_GRID = 24 };

static uint32_t cursor_color(char code) {
    /* Host canvas colors are 0xAABBGGRR. */
    switch (code) {
    case 'K': return UINT32_C(0xFF1F1018); /* ink outline */
    case 'W': return UINT32_C(0xFFEAC968); /* light FE blue */
    default: return 0;
    }
}

void fe8_host_draw_mouse_cursor(uint32_t *pixels, size_t stride,
    int width, int height, int hotspot_x, int hotspot_y) {
    /* A compact tactician's pointer. The continuous two-tone silhouette
     * stays readable without overpowering native FE8 menus and map tiles. */
    static const char *const art[CURSOR_GRID] = {
        "K.......................",
        "KWK.....................",
        "KWWK....................",
        "KWWWK...................",
        "KWWWWK..................",
        "KWWWWWK.................",
        "KWWWWWWK................",
        "KWWWWWWWK...............",
        "KWWWWWWWWK..............",
        "KWWWWWWWWWK.............",
        "KWWWWWWWWWWK............",
        "KWWWWWWWWWWWK...........",
        "KWWWWWWWWWWWWK..........",
        "KWWWWWWWWWWWWWK.........",
        "KWWWWWKKKKKKKKKK........",
        "KWWWWK.KWWWK............",
        "KWWWK...KWWWK...........",
        "KWWK....KWWWK...........",
        "KWK......KWWWK..........",
        "KK........KWWWK.........",
        "K.........KWWWK.........",
        "...........KWWWK........",
        "............KWWWK.......",
        ".............KKKK.......",
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
