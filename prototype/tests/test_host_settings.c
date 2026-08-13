#include "host_settings.h"

#include <assert.h>

int main(void) {
    Fe8HostSettings settings;
    fe8_host_settings_init(&settings);
    assert(settings.audio_enabled);
    assert(settings.vsync_enabled);
    assert(settings.extensions_enabled);
    assert(settings.shader == FE8_HOST_SHADER_OFF);
    assert(settings.hotkeys[FE8_HOST_HOTKEY_SPEED_UP] == SDL_SCANCODE_SPACE);
    assert(settings.hotkeys[FE8_HOST_HOTKEY_QUICK_SAVE] == SDL_SCANCODE_F5);
    assert(settings.hotkeys[FE8_HOST_HOTKEY_QUICK_LOAD] == SDL_SCANCODE_F8);
    assert(fe8_host_key_for_scancode(&settings, SDL_SCANCODE_Z) ==
        (UINT32_C(1) << FE8_HOST_A));
    assert(fe8_host_key_for_scancode(&settings, SDL_SCANCODE_RIGHT) ==
        (UINT32_C(1) << FE8_HOST_RIGHT));
    settings.bindings[FE8_HOST_B] = SDL_SCANCODE_Z;
    assert(fe8_host_key_for_scancode(&settings, SDL_SCANCODE_Z) ==
        ((UINT32_C(1) << FE8_HOST_A) | (UINT32_C(1) << FE8_HOST_B)));
    assert(fe8_host_button_name(FE8_HOST_START)[0] == 'S');
    assert(fe8_host_shader_name(FE8_HOST_SHADER_CRT)[0] == 'C');
    assert(fe8_host_hotkey_for_scancode(&settings, SDL_SCANCODE_SPACE) ==
        (UINT32_C(1) << FE8_HOST_HOTKEY_SPEED_UP));
    settings.hotkeys[FE8_HOST_HOTKEY_QUICK_LOAD] = SDL_SCANCODE_SPACE;
    assert(fe8_host_hotkey_for_scancode(&settings, SDL_SCANCODE_SPACE) ==
        ((UINT32_C(1) << FE8_HOST_HOTKEY_SPEED_UP) |
         (UINT32_C(1) << FE8_HOST_HOTKEY_QUICK_LOAD)));
    assert(fe8_host_hotkey_name(FE8_HOST_HOTKEY_QUICK_SAVE)[0] == 'Q');
    return 0;
}
