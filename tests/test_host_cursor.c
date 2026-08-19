#include "host_cursor.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

int main(void) {
    uint32_t pixels[32 * 32];
    memset(pixels, 0, sizeof(pixels));
    fe8_host_draw_mouse_cursor(pixels, 32, 32, 32, 2, 3);
    assert(pixels[3 * 32 + 2] != 0);  /* exact hotspot */
    assert(pixels[3 * 32 + 3] == 0);  /* transparent beside the tip */
    assert(pixels[8 * 32 + 4] != 0);  /* light-blue body */
    assert(pixels[19 * 32 + 12] != 0); /* cyan shaft interior */
    assert(pixels[25 * 32 + 16] != 0); /* straight diagonal shaft */
    assert(pixels[27 * 32 + 16] == 0); /* clean lower edge */
    assert(pixels[31 * 32 + 31] == 0);

    /* Clipping at an edge must not write outside the supplied canvas. */
    memset(pixels, 0, sizeof(pixels));
    fe8_host_draw_mouse_cursor(pixels, 32, 32, 32, 31, 31);
    assert(pixels[31 * 32 + 31] != 0);
    return 0;
}
