#include "host_settings.h"

#include <math.h>
#include <string.h>

typedef struct Fe8HostShaderPreset {
    const char *name;
    Fe8HostShaderConfig config;
} Fe8HostShaderPreset;

/* Keep each shader's display name and defaults together. Config fields are
 * scanlines, mask, blur, bloom, curvature, and saturation, in that order. */
static const Fe8HostShaderPreset shader_presets[FE8_HOST_SHADER_COUNT] = {
    [FE8_HOST_SHADER_OFF] = {
        "Off", {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}},
    [FE8_HOST_SHADER_CRT] = {
        "CRT (TV Mode)", {0.25f, 0.0f, 0.55f, 0.08f, 0.0f, 1.0f}},
    [FE8_HOST_SHADER_SCANLINES] = {
        "Scanlines", {0.50f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}},
    [FE8_HOST_SHADER_APERTURE_GRILLE] = {
        "CRT Aperture Grille", {0.18f, 0.35f, 0.08f, 0.05f, 0.0f, 1.0f}},
    [FE8_HOST_SHADER_SHADOW_MASK] = {
        "CRT Shadow Mask", {0.22f, 0.42f, 0.10f, 0.06f, 0.0f, 1.0f}},
    [FE8_HOST_SHADER_SLOT_MASK] = {
        "CRT Slot Mask", {0.24f, 0.48f, 0.12f, 0.06f, 0.0f, 1.0f}},
    [FE8_HOST_SHADER_CONSUMER_CRT] = {
        "CRT Consumer TV", {0.24f, 0.28f, 0.38f, 0.14f, 0.08f, 1.08f}},
    [FE8_HOST_SHADER_CRT_GEOM] = {
        "crt-geom", {0.30f, 0.0f, 0.18f, 0.06f, 0.115f, 1.03f}},
    [FE8_HOST_SHADER_CRT_LOTTES] = {
        "crt-lottes", {0.34f, 0.44f, 0.08f, 0.11f, 0.035f, 1.05f}},
    [FE8_HOST_SHADER_ZFAST_CRT] = {
        "zfast-crt", {0.24f, 0.10f, 0.06f, 0.02f, 0.0f, 1.0f}},
    [FE8_HOST_SHADER_CRT_APERTURE] = {
        "crt-aperture", {0.20f, 0.52f, 0.05f, 0.05f, 0.0f, 1.04f}},
    [FE8_HOST_SHADER_CRT_EASYMODE] = {
        "crt-easymode", {0.26f, 0.34f, 0.16f, 0.08f, 0.025f, 1.02f}},
};

static Fe8HostSettings *current_settings;
static Fe8HostShaderConfig shader_configs[FE8_HOST_SHADER_COUNT];
static int shader_configs_initialized;

static float clampf(float value, float minimum, float maximum) {
    if (!isfinite(value))
        return minimum;
    if (value < minimum)
        return minimum;
    if (value > maximum)
        return maximum;
    return value;
}

void fe8_host_shader_default_config(
    enum Fe8HostShader shader, Fe8HostShaderConfig *config) {
    if (!config)
        return;
    if (shader < 0 || shader >= FE8_HOST_SHADER_COUNT)
        shader = FE8_HOST_SHADER_OFF;
    *config = shader_presets[shader].config;
}

static void init_shader_configs(void) {
    int shader;
    if (shader_configs_initialized)
        return;
    for (shader = 0; shader < FE8_HOST_SHADER_COUNT; ++shader)
        fe8_host_shader_default_config(
            (enum Fe8HostShader)shader, &shader_configs[shader]);
    shader_configs_initialized = 1;
}

void fe8_host_shader_get_config(
    enum Fe8HostShader shader, Fe8HostShaderConfig *config) {
    init_shader_configs();
    if (!config)
        return;
    if (shader < 0 || shader >= FE8_HOST_SHADER_COUNT) {
        fe8_host_shader_default_config(FE8_HOST_SHADER_OFF, config);
        return;
    }
    *config = shader_configs[shader];
}

void fe8_host_shader_set_config(
    enum Fe8HostShader shader, const Fe8HostShaderConfig *config) {
    Fe8HostShaderConfig sanitized;
    init_shader_configs();
    if (!config || shader <= FE8_HOST_SHADER_OFF || shader >= FE8_HOST_SHADER_COUNT)
        return;
    sanitized.scanline_strength = clampf(config->scanline_strength, 0.0f, 1.0f);
    sanitized.mask_strength = clampf(config->mask_strength, 0.0f, 1.0f);
    sanitized.blur = clampf(config->blur, 0.0f, 1.0f);
    sanitized.bloom = clampf(config->bloom, 0.0f, 0.5f);
    sanitized.curvature = clampf(config->curvature, 0.0f, 0.25f);
    sanitized.saturation = clampf(config->saturation, 0.5f, 1.5f);
    shader_configs[shader] = sanitized;
}

void fe8_host_settings_init(Fe8HostSettings *settings) {
    static const SDL_Scancode defaults[FE8_HOST_BUTTON_COUNT] = {
        SDL_SCANCODE_Z,
        SDL_SCANCODE_X,
        SDL_SCANCODE_BACKSPACE,
        SDL_SCANCODE_RETURN,
        SDL_SCANCODE_RIGHT,
        SDL_SCANCODE_LEFT,
        SDL_SCANCODE_UP,
        SDL_SCANCODE_DOWN,
        SDL_SCANCODE_S,
        SDL_SCANCODE_A,
    };
    static const SDL_Scancode hotkey_defaults[FE8_HOST_HOTKEY_COUNT] = {
        SDL_SCANCODE_SPACE,
        SDL_SCANCODE_F5,
        SDL_SCANCODE_F8,
        SDL_SCANCODE_E,
    };
    memset(settings, 0, sizeof(*settings));
    memcpy(settings->bindings, defaults, sizeof(defaults));
    memcpy(settings->hotkeys, hotkey_defaults, sizeof(hotkey_defaults));
    settings->audio_enabled = 1;
    settings->vsync_enabled = 1;
    settings->extensions_enabled = 1;
    settings->mouse_enabled = 1;
    settings->shader = FE8_HOST_SHADER_OFF;
    settings->speedup_rate = FE8_HOST_SPEEDUP_4X;
    settings->zoom_sensitivity = FE8_HOST_ZOOM_SENSITIVITY_LOW;
    current_settings = settings;
    init_shader_configs();
}

Fe8HostSettings *fe8_host_settings_current(void) {
    return current_settings;
}

void fe8_host_set_extensions_enabled(Fe8HostSettings *settings, int enabled) {
    enabled = enabled != 0;
    if (settings && settings->extensions_enabled != enabled) {
        settings->extensions_enabled = enabled;
        ++settings->revision;
    }
}

double fe8_host_clamp_zoom_sensitivity(double sensitivity) {
    if (sensitivity < FE8_HOST_ZOOM_SENSITIVITY_LOW)
        return FE8_HOST_ZOOM_SENSITIVITY_LOW;
    if (sensitivity > FE8_HOST_ZOOM_SENSITIVITY_HIGH)
        return FE8_HOST_ZOOM_SENSITIVITY_HIGH;
    return sensitivity;
}

uint32_t fe8_host_hotkey_for_scancode(
    const Fe8HostSettings *settings, SDL_Scancode scancode) {
    uint32_t hotkeys = 0;
    unsigned hotkey;
    for (hotkey = 0; hotkey < FE8_HOST_HOTKEY_COUNT; ++hotkey)
        if (settings->hotkeys[hotkey] == scancode)
            hotkeys |= UINT32_C(1) << hotkey;
    return hotkeys;
}

uint32_t fe8_host_key_for_scancode(
    const Fe8HostSettings *settings, SDL_Scancode scancode) {
    uint32_t keys = 0;
    unsigned button;
    for (button = 0; button < FE8_HOST_BUTTON_COUNT; ++button)
        if (settings->bindings[button] == scancode)
            keys |= UINT32_C(1) << button;
    return keys;
}

const char *fe8_host_button_name(enum Fe8HostButton button) {
    static const char *names[FE8_HOST_BUTTON_COUNT] = {
        "A", "B", "Select", "Start", "Right", "Left", "Up", "Down", "R", "L"
    };
    return button >= 0 && button < FE8_HOST_BUTTON_COUNT ? names[button] : "Unknown";
}

const char *fe8_host_hotkey_name(enum Fe8HostHotkey hotkey) {
    static const char *names[FE8_HOST_HOTKEY_COUNT] = {
        "Speed Up", "Quick Save", "Quick Load", "Extended Renderer"
    };
    return hotkey >= 0 && hotkey < FE8_HOST_HOTKEY_COUNT ? names[hotkey] : "Unknown";
}

const char *fe8_host_shader_name(enum Fe8HostShader shader) {
    if (shader < 0 || shader >= FE8_HOST_SHADER_COUNT)
        shader = FE8_HOST_SHADER_OFF;
    return shader_presets[shader].name;
}

const char *fe8_host_speedup_name(enum Fe8HostSpeedupRate rate) {
    static const char *names[FE8_HOST_SPEEDUP_COUNT] = {
        "2×", "3×", "4×", "Unlimited"
    };
    return rate >= 0 && rate < FE8_HOST_SPEEDUP_COUNT ? names[rate] : "4×";
}

unsigned fe8_host_speedup_multiplier(enum Fe8HostSpeedupRate rate) {
    static const unsigned multipliers[FE8_HOST_SPEEDUP_COUNT] = {2, 3, 4, 0};
    return rate >= 0 && rate < FE8_HOST_SPEEDUP_COUNT ? multipliers[rate] : 4;
}
