#include "video_layout.h"

#include <assert.h>
#include <stdio.h>

static void buggy_backend_set_size(
    int desired_width, int desired_height,
    int *cached_width, int *cached_height) {
    if (desired_width != *cached_width &&
            desired_height != *cached_height) {
        *cached_width = desired_width;
        *cached_height = desired_height;
    }
}

static void compatible_backend_set_size(
    int desired_width, int desired_height,
    int *cached_width, int *cached_height) {
    fe8_video_layout_prepare_mgles2_cache(
        desired_width, desired_height, cached_width, cached_height);
    buggy_backend_set_size(
        desired_width, desired_height, cached_width, cached_height);
}

int main(void) {
    int width = 480;
    int height = 320;

    /* mGLES2's current all-axes check ignores this adaptive widescreen resize. */
    buggy_backend_set_size(513, 320, &width, &height);
    assert(width == 480 && height == 320);

    compatible_backend_set_size(513, 320, &width, &height);
    assert(width == 513 && height == 320);

    compatible_backend_set_size(513, 319, &width, &height);
    assert(width == 513 && height == 319);

    compatible_backend_set_size(510, 318, &width, &height);
    assert(width == 510 && height == 318);

    compatible_backend_set_size(510, 318, &width, &height);
    assert(width == 510 && height == 318);

    fe8_video_layout_prepare_mgles2_cache(511, 318, NULL, &height);
    assert(height == 318);

    puts("video layout tests passed");
    return 0;
}
