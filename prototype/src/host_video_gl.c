#include "host_video.h"

#include "gles2.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Fe8HostVideoGl {
    SDL_GLContext gl_context;
    struct mGLES2Context renderer;
    struct mGLES2Shader shader;
    struct mGLES2Uniform uniforms[2];
    int renderer_initialized;
    int shader_initialized;
    int drawable_width;
    int drawable_height;
} Fe8HostVideoGl;

/* These are the fragment passes shipped by mGBA. TV Mode and Scanlines are
 * Copyright (C) Dominus Iniquitatis and distributed under the MIT license;
 * see THIRD_PARTY_NOTICES.md. The actual compilation, pass sizing, uniforms,
 * filtering, and presentation are handled by mGLES2Shader/mGLES2Context. */
static const char *const crt_fragment_shader =
    "uniform sampler2D tex;\n"
    "uniform vec2 texSize;\n"
    "varying vec2 texCoord;\n"
    "uniform float lineBrightness;\n"
    "uniform float blurring;\n"
    "void main() {\n"
    " vec4 c = texture2D(tex, texCoord);\n"
    " vec4 n = texture2D(tex, texCoord + vec2(1.0 / texSize.x * 0.5, 0.0));\n"
    " vec4 color = mix(c, (c + n) / 2.0, blurring);\n"
    " color.a = c.a;\n"
    " if (int(mod(texCoord.t * texSize.y * 2.0, 2.0)) == 0)\n"
    "  color.rgb *= lineBrightness;\n"
    " gl_FragColor = color;\n"
    "}\n";

static const char *const scanlines_fragment_shader =
    "uniform sampler2D tex;\n"
    "uniform vec2 texSize;\n"
    "varying vec2 texCoord;\n"
    "uniform float lineBrightness;\n"
    "void main() {\n"
    " vec4 color = texture2D(tex, texCoord);\n"
    " if (int(mod(texCoord.t * texSize.y * 2.0, 2.0)) == 0)\n"
    "  color.rgb *= lineBrightness;\n"
    " gl_FragColor = color;\n"
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

int fe8_host_video_init(Fe8HostVideo *video, const char *title,
    int canvas_width, int canvas_height, int vsync_enabled) {
    Fe8HostVideoGl *backend;
    struct mRectangle image = {0, 0, canvas_width, canvas_height};
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
    backend->renderer.d.setImageSize(&backend->renderer.d,
        VIDEO_LAYER_IMAGE, canvas_width, canvas_height);
    backend->renderer.d.setLayerDimensions(&backend->renderer.d,
        VIDEO_LAYER_IMAGE, &image);
    SDL_GL_GetDrawableSize(video->window,
        &backend->drawable_width, &backend->drawable_height);
    backend->renderer.d.contextResized(&backend->renderer.d,
        (unsigned)backend->drawable_width, (unsigned)backend->drawable_height, 0, 0);
    video->canvas_width = canvas_width;
    video->canvas_height = canvas_height;
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
    const char *fragment;
    size_t uniform_count;
    if (!backend || mode < 0 || mode >= FE8_HOST_SHADER_COUNT)
        return 0;
    if (video->shader == mode &&
            (mode == FE8_HOST_SHADER_OFF || backend->shader_initialized))
        return 1;
    destroy_shader(backend);
    video->shader = FE8_HOST_SHADER_OFF;
    if (mode == FE8_HOST_SHADER_OFF)
        return 1;

    memset(backend->uniforms, 0, sizeof(backend->uniforms));
    backend->uniforms[0].name = "lineBrightness";
    backend->uniforms[0].readableName = "Line brightness";
    backend->uniforms[0].type = GL_FLOAT;
    backend->uniforms[0].value.f = mode == FE8_HOST_SHADER_CRT ? 0.75f : 0.5f;
    uniform_count = 1;
    fragment = scanlines_fragment_shader;
    if (mode == FE8_HOST_SHADER_CRT) {
        backend->uniforms[1].name = "blurring";
        backend->uniforms[1].readableName = "Blurring";
        backend->uniforms[1].type = GL_FLOAT;
        backend->uniforms[1].value.f = 1.0f;
        uniform_count = 2;
        fragment = crt_fragment_shader;
    }
    mGLES2ShaderInit(&backend->shader, NULL, fragment,
        -2, -2, false, backend->uniforms, uniform_count);
    backend->shader.blend = true;
    backend->shader_initialized = 1;
    if (!shader_linked(&backend->shader)) {
        destroy_shader(backend);
        return 0;
    }
    mGLES2ShaderAttach(&backend->renderer, &backend->shader, 1);
    video->shader = mode;
    return 1;
}

int fe8_host_video_present(Fe8HostVideo *video, const void *pixels) {
    Fe8HostVideoGl *backend = video ? video->backend : NULL;
    int width;
    int height;
    if (!backend || !pixels)
        return 0;
    while (glGetError() != GL_NO_ERROR) {}
    SDL_GL_GetDrawableSize(video->window, &width, &height);
    if (width != backend->drawable_width || height != backend->drawable_height) {
        backend->drawable_width = width;
        backend->drawable_height = height;
        backend->renderer.d.contextResized(&backend->renderer.d,
            (unsigned)width, (unsigned)height, 0, 0);
    }
    backend->renderer.d.clear(&backend->renderer.d);
    backend->renderer.d.setImage(&backend->renderer.d, VIDEO_LAYER_IMAGE, pixels);
    backend->renderer.d.drawFrame(&backend->renderer.d);
    SDL_GL_SwapWindow(video->window);
    return glGetError() == GL_NO_ERROR;
}

void fe8_host_video_log_status(const Fe8HostVideo *video) {
    const Fe8HostVideoGl *backend = video ? video->backend : NULL;
    int window_width;
    int window_height;
    if (!backend)
        return;
    SDL_GetWindowSize(video->window, &window_width, &window_height);
    fprintf(stderr, "Display: window=%dx%d output=%dx%d logical=%dx%d backend=mGLES2\n",
        window_width, window_height, backend->drawable_width, backend->drawable_height,
        video->canvas_width, video->canvas_height);
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
