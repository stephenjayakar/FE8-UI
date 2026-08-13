#ifndef FE8_HOST_SETTINGS_H
#define FE8_HOST_SETTINGS_H

#include <SDL.h>
#include <stdint.h>

enum Fe8HostButton {
    FE8_HOST_A = 0,
    FE8_HOST_B,
    FE8_HOST_SELECT,
    FE8_HOST_START,
    FE8_HOST_RIGHT,
    FE8_HOST_LEFT,
    FE8_HOST_UP,
    FE8_HOST_DOWN,
    FE8_HOST_R,
    FE8_HOST_L,
    FE8_HOST_BUTTON_COUNT
};

enum Fe8HostShader {
    FE8_HOST_SHADER_OFF = 0,
    FE8_HOST_SHADER_CRT,
    FE8_HOST_SHADER_SCANLINES,
    FE8_HOST_SHADER_COUNT
};

enum Fe8HostHotkey {
    FE8_HOST_HOTKEY_SPEED_UP = 0,
    FE8_HOST_HOTKEY_QUICK_SAVE,
    FE8_HOST_HOTKEY_QUICK_LOAD,
    FE8_HOST_HOTKEY_COUNT
};

#define FE8_HOST_ZOOM_SENSITIVITY_LOW 0.005
#define FE8_HOST_ZOOM_SENSITIVITY_HIGH 0.030

typedef struct Fe8HostSettings {
    SDL_Scancode bindings[FE8_HOST_BUTTON_COUNT];
    SDL_Scancode hotkeys[FE8_HOST_HOTKEY_COUNT];
    int audio_enabled;
    int vsync_enabled;
    int extensions_enabled;
    int mouse_enabled;
    enum Fe8HostShader shader;
    double zoom_sensitivity;
    unsigned revision;
} Fe8HostSettings;

void fe8_host_settings_init(Fe8HostSettings *settings);
uint32_t fe8_host_key_for_scancode(
    const Fe8HostSettings *settings, SDL_Scancode scancode);
uint32_t fe8_host_hotkey_for_scancode(
    const Fe8HostSettings *settings, SDL_Scancode scancode);
const char *fe8_host_button_name(enum Fe8HostButton button);
const char *fe8_host_hotkey_name(enum Fe8HostHotkey hotkey);
const char *fe8_host_shader_name(enum Fe8HostShader shader);
double fe8_host_clamp_zoom_sensitivity(double sensitivity);

#endif
