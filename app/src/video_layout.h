#ifndef FE8_VIDEO_LAYOUT_H
#define FE8_VIDEO_LAYOUT_H

/* The pinned mGLES2 backend commits a layer size only when both cached axes
 * differ. Adaptive FE8 canvases commonly change just one axis. Make the
 * unchanged cached axis temporarily unequal so the backend's existing setter
 * applies the complete rectangle. */
static inline void fe8_video_layout_prepare_mgles2_cache(
    int desired_width, int desired_height,
    int *cached_width, int *cached_height) {
    if (!cached_width || !cached_height)
        return;
    if (desired_width != *cached_width &&
            desired_height == *cached_height)
        *cached_height = -1;
    else if (desired_width == *cached_width &&
            desired_height != *cached_height)
        *cached_width = -1;
}

#endif
