#include "gles2.h"
#include "video_layout.h"

typedef void (*Fe8SetLayerDimensions)(
    struct VideoBackend *video, enum VideoLayer layer,
    const struct mRectangle *dimensions);

static Fe8SetLayerDimensions original_set_layer_dimensions;

static void set_layer_dimensions_compat(
    struct VideoBackend *video, enum VideoLayer layer,
    const struct mRectangle *dimensions) {
    struct mGLES2Context *context = (struct mGLES2Context *)video;
    if (context && dimensions && (unsigned)layer < VIDEO_LAYER_MAX) {
        struct mRectangle *cached = &context->layerDims[layer];
        fe8_video_layout_prepare_mgles2_cache(
            dimensions->width, dimensions->height,
            &cached->width, &cached->height);
    }
    if (original_set_layer_dimensions)
        original_set_layer_dimensions(video, layer, dimensions);
}

void fe8_mgles2_context_create(struct mGLES2Context *context) {
    if (!context)
        return;
    mGLES2ContextCreate(context);
    original_set_layer_dimensions = context->d.setLayerDimensions;
    context->d.setLayerDimensions = set_layer_dimensions_compat;
}
