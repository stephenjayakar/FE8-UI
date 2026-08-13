#include "host_settings.h"

#include <string.h>

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
    };
    memset(settings, 0, sizeof(*settings));
    memcpy(settings->bindings, defaults, sizeof(defaults));
    memcpy(settings->hotkeys, hotkey_defaults, sizeof(hotkey_defaults));
    settings->audio_enabled = 1;
    settings->vsync_enabled = 1;
    settings->extensions_enabled = 1;
    settings->shader = FE8_HOST_SHADER_OFF;
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
        "Speed Up", "Quick Save", "Quick Load"
    };
    return hotkey >= 0 && hotkey < FE8_HOST_HOTKEY_COUNT ? names[hotkey] : "Unknown";
}

const char *fe8_host_shader_name(enum Fe8HostShader shader) {
    static const char *names[FE8_HOST_SHADER_COUNT] = {
        "Off", "CRT (TV Mode)", "Scanlines"
    };
    return shader >= 0 && shader < FE8_HOST_SHADER_COUNT ? names[shader] : "Off";
}
