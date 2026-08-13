#include "host_settings.h"

#include <assert.h>

int main(void) {
    Fe8HostSettings settings;
    fe8_host_settings_init(&settings);
    assert(settings.audio_enabled);
    assert(settings.vsync_enabled);
    assert(settings.extensions_enabled);
    assert(settings.mouse_enabled);
    assert(settings.shader == FE8_HOST_SHADER_OFF);
    assert(fe8_host_key_for_scancode(&settings, SDL_SCANCODE_Z) ==
        (UINT32_C(1) << FE8_HOST_A));
    assert(fe8_host_key_for_scancode(&settings, SDL_SCANCODE_RIGHT) ==
        (UINT32_C(1) << FE8_HOST_RIGHT));
    settings.bindings[FE8_HOST_B] = SDL_SCANCODE_Z;
    assert(fe8_host_key_for_scancode(&settings, SDL_SCANCODE_Z) ==
        ((UINT32_C(1) << FE8_HOST_A) | (UINT32_C(1) << FE8_HOST_B)));
    assert(fe8_host_button_name(FE8_HOST_START)[0] == 'S');
    assert(fe8_host_shader_name(FE8_HOST_SHADER_CRT)[0] == 'C');
    return 0;
}
