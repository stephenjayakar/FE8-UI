#include "host_video.h"

#include "gles2.h"
#include "pointer_mapping.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Fe8HostVideoGl {
    SDL_GLContext gl_context;
    struct mGLES2Context renderer;
    struct mGLES2Shader shader;
    struct mGLES2Uniform uniforms[7];
    int renderer_initialized;
    int shader_initialized;
    int drawable_width;
    int drawable_height;
} Fe8HostVideoGl;

/* A single configurable CRT pass keeps preset changes cheap: switching modes
 * or moving a slider only updates uniforms instead of rebuilding the GL
 * program. The pass intentionally uses only mGLES2's portable shader inputs so
 * it runs in the existing macOS OpenGL 3.2 compatibility wrapper. */
static const char *const crt_fragment_shader =
    "uniform sampler2D tex;\n"
    "uniform vec2 texSize;\n"
    "uniform vec2 outputSize;\n"
    "varying vec2 texCoord;\n"
    "uniform int crtMode;\n"
    "uniform float scanlineStrength;\n"
    "uniform float maskStrength;\n"
    "uniform float blurAmount;\n"
    "uniform float bloomAmount;\n"
    "uniform float curvature;\n"
    "uniform float saturation;\n"
    "float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }\n"
    "void main() {\n"
    " vec2 p = texCoord * 2.0 - 1.0;\n"
    " p *= 1.0 + curvature * vec2(p.y * p.y, p.x * p.x);\n"
    " vec2 uv = p * 0.5 + 0.5;\n"
    " if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {\n"
    "  gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0); return;\n"
    " }\n"
    " vec2 px = 1.0 / texSize;\n"
    " vec4 center = texture2D(tex, uv);\n"
    " vec3 soft = (texture2D(tex, uv - vec2(px.x, 0.0)).rgb +\n"
    "              texture2D(tex, uv + vec2(px.x, 0.0)).rgb) * 0.5;\n"
    " vec3 color = mix(center.rgb, soft, blurAmount * 0.5);\n"
    " vec3 glow = (texture2D(tex, uv - px).rgb + texture2D(tex, uv + px).rgb) * 0.5;\n"
    " color += max(glow - color, vec3(0.0)) * bloomAmount;\n"
    " float gray = luma(color);\n"
    " color = mix(vec3(gray), color, saturation);\n"
    " float scan = 0.5 + 0.5 * cos(uv.y * texSize.y * 6.28318530718);\n"
    " color *= 1.0 - scanlineStrength * 0.5 * scan;\n"
    " vec3 mask = vec3(1.0);\n"
    " float ox = floor(uv.x * outputSize.x);\n"
    " float oy = floor(uv.y * outputSize.y);\n"
    " if (crtMode == 3 || crtMode == 6) {\n"
    "  float triad = mod(ox, 3.0);\n"
    "  mask = triad < 1.0 ? vec3(1.0, 0.72, 0.72) :\n"
    "         (triad < 2.0 ? vec3(0.72, 1.0, 0.72) : vec3(0.72, 0.72, 1.0));\n"
    " } else if (crtMode == 4) {\n"
    "  float triad = mod(ox + mod(oy, 2.0) * 1.5, 3.0);\n"
    "  mask = triad < 1.0 ? vec3(1.0, 0.67, 0.67) :\n"
    "         (triad < 2.0 ? vec3(0.67, 1.0, 0.67) : vec3(0.67, 0.67, 1.0));\n"
    "  if (mod(oy, 4.0) >= 3.0) mask *= 0.88;\n"
    " } else if (crtMode == 5) {\n"
    "  float triad = mod(ox, 6.0);\n"
    "  mask = triad < 2.0 ? vec3(1.0, 0.68, 0.68) :\n"
    "         (triad < 4.0 ? vec3(0.68, 1.0, 0.68) : vec3(0.68, 0.68, 1.0));\n"
    "  if (mod(oy, 3.0) >= 2.0) mask *= 0.80;\n"
    " }\n"
    " color *= mix(vec3(1.0), mask, maskStrength);\n"
    " gl_FragColor = vec4(max(color, vec3(0.0)), center.a);\n"
    "}\n";

static void destroy_shader(Fe8HostVideoGl *backend) {
    GLuint vertex_shader;
    if (!backend->shader_initialized)
        return;
    mGLES2ShaderDetach(&backend->renderer);
    vertex_shader = backend->shader.vertexShader;
    mGLES2ShaderDeinit(&backend->shader);
    if (vertex_shader)
        glDeleteShader(vertex_shader);
    memset(&backend->shader, 0, sizeof(backend->shader));
    memset(backend->uniforms, 0, sizeof(backend->uniforms));
    backend->shader_initialized = 0;
}

static int shader_linked(const struct mGLES2Shader *shader) {
    GLint linked = GL_FALSE;
    glGetProgramiv(shader->program, GL_LINK_STATUS, &linked);
    return linked == GL_TRUE;
}

static void update_shader_uniforms(
    Fe8HostVideoGl *backend, enum Fe8HostShader mode) {
    Fe8HostShaderConfig config;
    fe8_host_shader_get_config(mode, &config);
    backend->uniforms[0].value.i = (GLint)mode;
    backend->uniforms[1].value.f = config.scanline_strength;
    backend->uniforms[2].value.f = config.mask_strength;
    backend->uniforms[3].value.f = config.blur;
    backend->uniforms[4].value.f = config.bloom;
    backend->uniforms[5].value.f = config.curvature;
    backend->uniforms[6].value.f = config.saturation;
}

static void apply_layout(Fe8HostVideo *video, Fe8HostVideoGl *backend) {
    struct mRectangle image = {
        0, 0, video->scaling.canvas_width, video->scaling.canvas_height};
    video->canvas_width = image.width;
    video->canvas_height = image.height;
    backend->renderer.d.setImageSize(&backend->renderer.d,
        VIDEO_LAYER_IMAGE, image.width, image.height);
    backend->renderer.d.setLayerDimensions(&backend->renderer.d,
        VIDEO_LAYER_IMAGE, &image);
    backend->renderer.d.contextResized(&backend->renderer.d,
        (unsigned)backend->drawable_width, (unsigned)backend->drawable_height, 0, 0);
}

int fe8_host_video_init(Fe8HostVideo *video, const char *title,
    int canvas_width, int canvas_height, int vsync_enabled) {
    Fe8HostVideoGl *backend;
    memset(video, 0, sizeof(*video));
    if (SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3) != 0 ||
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2) != 0 ||
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                SDL_GL_CONTEXT_PROFILE_CORE) != 0 ||
            SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1) != 0)
        return 0;
    video->window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED, canvas_width * 2, canvas_height * 2,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI |
            SDL_WINDOW_MAXIMIZED);
    if (!video->window)
        return 0;
    backend = calloc(1, sizeof(*backend));
    if (!backend)
        return 0;
    video->backend = backend;
    backend->gl_context = SDL_GL_CreateContext(video->window);
    if (!backend->gl_context ||
            SDL_GL_MakeCurrent(video->window, backend->gl_context) != 0)
        return 0;

    mGLES2ContextCreate(&backend->renderer);
    backend->renderer.d.lockAspectRatio = true;
    backend->renderer.d.lockIntegerScaling = false;
    backend->renderer.d.filter = false;
    backend->renderer.d.init(&backend->renderer.d, NULL);
    backend->renderer_initialized = 1;
    SDL_GL_GetDrawableSize(video->window,
        &backend->drawable_width, &backend->drawable_height);
    fe8_display_scaling_init(&video->scaling,
        canvas_width, canvas_height, 240, 160);
    video->base_canvas_width = canvas_width;
    video->base_canvas_height = canvas_height;
    fe8_display_scaling_resize(&video->scaling,
        backend->drawable_width, backend->drawable_height);
    apply_layout(video, backend);
    video->shader = FE8_HOST_SHADER_OFF;
    fe8_host_video_set_vsync(video, vsync_enabled);
    return 1;
}

int fe8_host_video_set_vsync(Fe8HostVideo *video, int enabled) {
    if (!video || !video->window)
        return 0;
    if (SDL_GL_SetSwapInterval(enabled ? 1 : 0) != 0)
        return 0;
    video->vsync_active = enabled != 0;
    return 1;
}

int fe8_host_video_set_shader(Fe8HostVideo *video, enum Fe8HostShader mode) {
    Fe8HostVideoGl *backend = video ? video->backend : NULL;
    static const char *const names[] = {
        "crtMode", "scanlineStrength", "maskStrength", "blurAmount",
        "bloomAmount", "curvature", "saturation"
    };
    static const char *const readable[] = {
        "CRT mode", "Scanlines", "Mask", "Blur", "Bloom", "Curvature", "Saturation"
    };
    size_t i;
    if (!backend || mode < 0 || mode >= FE8_HOST_SHADER_COUNT)
        return 0;
    if (mode == FE8_HOST_SHADER_OFF) {
        destroy_shader(backend);
        video->shader = FE8_HOST_SHADER_OFF;
        return 1;
    }
    if (backend->shader_initialized) {
        update_shader_uniforms(backend, mode);
        video->shader = mode;
        return 1;
    }

    memset(backend->uniforms, 0, sizeof(backend->uniforms));
    for (i = 0; i < sizeof(backend->uniforms) / sizeof(backend->uniforms[0]); ++i) {
        backend->uniforms[i].name = names[i];
        backend->uniforms[i].readableName = readable[i];
        backend->uniforms[i].type = i == 0 ? GL_INT : GL_FLOAT;
    }
    update_shader_uniforms(backend, mode);
    mGLES2ShaderInit(&backend->shader, NULL, crt_fragment_shader,
        -2, -2, false, backend->uniforms,
        sizeof(backend->uniforms) / sizeof(backend->uniforms[0]));
    backend->shader.blend = true;
    backend->shader_initialized = 1;
    if (!shader_linked(&backend->shader)) {
        destroy_shader(backend);
        video->shader = FE8_HOST_SHADER_OFF;
        return 0;
    }
    mGLES2ShaderAttach(&backend->renderer, &backend->shader, 1);
    video->shader = mode;
    return 1;
}

int fe8_host_video_present(Fe8HostVideo *video, const void *pixels) {
    Fe8HostVideoGl *backend = video ? video->backend : NULL;
    if (!backend || !pixels)
        return 0;
    while (glGetError() != GL_NO_ERROR) {}
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    backend->renderer.d.clear(&backend->renderer.d);
    backend->renderer.d.setImage(&backend->renderer.d, VIDEO_LAYER_IMAGE, pixels);
    backend->renderer.d.drawFrame(&backend->renderer.d);
    SDL_GL_SwapWindow(video->window);
    return glGetError() == GL_NO_ERROR;
}

int fe8_host_video_window_to_canvas(const Fe8HostVideo *video,
    int window_x, int window_y, int *canvas_x, int *canvas_y) {
    int window_width;
    int window_height;
    int drawable_width;
    int drawable_height;
    if (!video || !video->window)
        return 0;
    SDL_GetWindowSize(video->window, &window_width, &window_height);
    SDL_GL_GetDrawableSize(video->window, &drawable_width, &drawable_height);
    return fe8_pointer_window_to_canvas(window_width, window_height,
        drawable_width, drawable_height, video->canvas_width, video->canvas_height,
        window_x, window_y, canvas_x, canvas_y);
}

int fe8_host_video_adjust_zoom(
    Fe8HostVideo *video, double wheel_delta, double sensitivity) {
    Fe8HostVideoGl *backend = video ? video->backend : NULL;
    if (!backend || !video->window)
        return 0;
    if (!fe8_display_scaling_adjust(
            &video->scaling, wheel_delta, sensitivity))
        return 0;
    apply_layout(video, backend);
    return 1;
}

int fe8_host_video_refresh_layout(Fe8HostVideo *video) {
    Fe8HostVideoGl *backend = video ? video->backend : NULL;
    int width;
    int height;
    if (!backend || !video->window)
        return 0;
    SDL_GL_GetDrawableSize(video->window, &width, &height);
    if (width == backend->drawable_width && height == backend->drawable_height)
        return 0;
    backend->drawable_width = width;
    backend->drawable_height = height;
    if (fe8_display_scaling_resize(&video->scaling, width, height)) {
        apply_layout(video, backend);
        return 1;
    }
    backend->renderer.d.contextResized(&backend->renderer.d,
        (unsigned)width, (unsigned)height, 0, 0);
    return 0;
}

int fe8_host_video_set_content_density(Fe8HostVideo *video, int density) {
    Fe8HostVideoGl *backend = video ? video->backend : NULL;
    if (!backend || density < 1 || density > 3)
        return 0;
    video->scaling.minimum_canvas_width = video->base_canvas_width * density;
    video->scaling.minimum_canvas_height = video->base_canvas_height * density;
    fe8_display_scaling_resize(&video->scaling,
        backend->drawable_width, backend->drawable_height);
    apply_layout(video, backend);
    return 1;
}

void fe8_host_video_log_status(const Fe8HostVideo *video) {
    const Fe8HostVideoGl *backend = video ? video->backend : NULL;
    int window_width;
    int window_height;
    if (!backend)
        return;
    SDL_GetWindowSize(video->window, &window_width, &window_height);
    fprintf(stderr, "Display: window=%dx%d output=%dx%d logical=%dx%d scale=%.2fx backend=mGLES2\n",
        window_width, window_height, backend->drawable_width, backend->drawable_height,
        video->canvas_width, video->canvas_height, video->scaling.pixel_scale);
}

void fe8_host_video_deinit(Fe8HostVideo *video) {
    Fe8HostVideoGl *backend = video ? video->backend : NULL;
    if (!video)
        return;
    if (backend) {
        if (video->window && backend->gl_context)
            SDL_GL_MakeCurrent(video->window, backend->gl_context);
        destroy_shader(backend);
        if (backend->renderer_initialized)
            backend->renderer.d.deinit(&backend->renderer.d);
        if (backend->gl_context)
            SDL_GL_DeleteContext(backend->gl_context);
        free(backend);
    }
    if (video->window)
        SDL_DestroyWindow(video->window);
    memset(video, 0, sizeof(*video));
}
