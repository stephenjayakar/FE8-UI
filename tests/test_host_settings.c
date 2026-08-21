#include "host_settings.h"

#include <assert.h>
#include <string.h>

int main(void) {
    Fe8HostSettings settings;
    Fe8HostShaderConfig config;
    fe8_host_settings_init(&settings);
    assert(settings.audio_enabled);
    assert(settings.vsync_enabled);
    assert(settings.extensions_enabled);
    assert(settings.mouse_enabled);
    assert(settings.shader == FE8_HOST_SHADER_OFF);
    assert(settings.speedup_rate == FE8_HOST_SPEEDUP_4X);
    assert(fe8_host_speedup_multiplier(FE8_HOST_SPEEDUP_2X) == 2);
    assert(fe8_host_speedup_multiplier(FE8_HOST_SPEEDUP_3X) == 3);
    assert(fe8_host_speedup_multiplier(FE8_HOST_SPEEDUP_4X) == 4);
    assert(fe8_host_speedup_multiplier(FE8_HOST_SPEEDUP_UNLIMITED) == 0);
    assert(fe8_host_speedup_name(FE8_HOST_SPEEDUP_UNLIMITED)[0] == 'U');
    assert(settings.zoom_sensitivity == FE8_HOST_ZOOM_SENSITIVITY_LOW);
    assert(fe8_host_clamp_zoom_sensitivity(0.0) ==
        FE8_HOST_ZOOM_SENSITIVITY_LOW);
    assert(fe8_host_clamp_zoom_sensitivity(1.0) ==
        FE8_HOST_ZOOM_SENSITIVITY_HIGH);
    assert(fe8_host_clamp_zoom_sensitivity(0.0125) == 0.0125);
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

    assert(strcmp(fe8_host_shader_name(FE8_HOST_SHADER_CRT_GEOM), "crt-geom") == 0);
    assert(strcmp(fe8_host_shader_name(FE8_HOST_SHADER_CRT_LOTTES), "crt-lottes") == 0);
    assert(strcmp(fe8_host_shader_name(FE8_HOST_SHADER_ZFAST_CRT), "zfast-crt") == 0);
    assert(strcmp(fe8_host_shader_name(FE8_HOST_SHADER_CRT_APERTURE), "crt-aperture") == 0);
    assert(strcmp(fe8_host_shader_name(FE8_HOST_SHADER_CRT_EASYMODE), "crt-easymode") == 0);

    fe8_host_shader_default_config(FE8_HOST_SHADER_CRT_GEOM, &config);
    assert(config.curvature > 0.10f);
    assert(config.scanline_strength > 0.0f);
    fe8_host_shader_default_config(FE8_HOST_SHADER_CRT_LOTTES, &config);
    assert(config.mask_strength > 0.40f);
    assert(config.bloom > 0.0f);
    fe8_host_shader_default_config(FE8_HOST_SHADER_ZFAST_CRT, &config);
    assert(config.blur < 0.10f);
    assert(config.bloom < 0.05f);
    fe8_host_shader_default_config(FE8_HOST_SHADER_CRT_APERTURE, &config);
    assert(config.mask_strength > 0.50f);
    fe8_host_shader_default_config(FE8_HOST_SHADER_CRT_EASYMODE, &config);
    assert(config.mask_strength > 0.30f);
    assert(config.blur > 0.10f);

    assert(fe8_host_hotkey_for_scancode(&settings, SDL_SCANCODE_SPACE) ==
        (UINT32_C(1) << FE8_HOST_HOTKEY_SPEED_UP));
    settings.hotkeys[FE8_HOST_HOTKEY_QUICK_LOAD] = SDL_SCANCODE_SPACE;
    assert(fe8_host_hotkey_for_scancode(&settings, SDL_SCANCODE_SPACE) ==
        ((UINT32_C(1) << FE8_HOST_HOTKEY_SPEED_UP) |
         (UINT32_C(1) << FE8_HOST_HOTKEY_QUICK_LOAD)));
    assert(fe8_host_hotkey_name(FE8_HOST_HOTKEY_QUICK_SAVE)[0] == 'Q');
    return 0;
}
