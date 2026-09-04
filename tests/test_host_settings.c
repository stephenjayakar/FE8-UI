#include "host_settings.h"

#include <assert.h>
#include <math.h>
#include <string.h>

static void assert_config_equal(
    const Fe8HostShaderConfig *actual, const Fe8HostShaderConfig *expected) {
    assert(actual->scanline_strength == expected->scanline_strength);
    assert(actual->mask_strength == expected->mask_strength);
    assert(actual->blur == expected->blur);
    assert(actual->bloom == expected->bloom);
    assert(actual->curvature == expected->curvature);
    assert(actual->saturation == expected->saturation);
}

static void test_shader_presets(void) {
    static const char *names[FE8_HOST_SHADER_COUNT] = {
        "Off", "CRT (TV Mode)", "Scanlines", "CRT Aperture Grille",
        "CRT Shadow Mask", "CRT Slot Mask", "CRT Consumer TV", "crt-geom",
        "crt-lottes", "zfast-crt", "crt-aperture", "crt-easymode"
    };
    /* Independent expectations preserve all existing preset values and IDs. */
    static const Fe8HostShaderConfig defaults[FE8_HOST_SHADER_COUNT] = {
        {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f},
        {0.25f, 0.0f, 0.55f, 0.08f, 0.0f, 1.0f},
        {0.50f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f},
        {0.18f, 0.35f, 0.08f, 0.05f, 0.0f, 1.0f},
        {0.22f, 0.42f, 0.10f, 0.06f, 0.0f, 1.0f},
        {0.24f, 0.48f, 0.12f, 0.06f, 0.0f, 1.0f},
        {0.24f, 0.28f, 0.38f, 0.14f, 0.08f, 1.08f},
        {0.30f, 0.0f, 0.18f, 0.06f, 0.115f, 1.03f},
        {0.34f, 0.44f, 0.08f, 0.11f, 0.035f, 1.05f},
        {0.24f, 0.10f, 0.06f, 0.02f, 0.0f, 1.0f},
        {0.20f, 0.52f, 0.05f, 0.05f, 0.0f, 1.04f},
        {0.26f, 0.34f, 0.16f, 0.08f, 0.025f, 1.02f},
    };
    Fe8HostShaderConfig config;
    int shader;
    for (shader = 0; shader < FE8_HOST_SHADER_COUNT; ++shader) {
        assert(strcmp(fe8_host_shader_name(shader), names[shader]) == 0);
        fe8_host_shader_default_config(shader, &config);
        assert_config_equal(&config, &defaults[shader]);
        fe8_host_shader_get_config(shader, &config);
        assert_config_equal(&config, &defaults[shader]);
    }
    {
        const enum Fe8HostShader invalid[] = {-1, FE8_HOST_SHADER_COUNT};
        size_t i;
        for (i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
            assert(strcmp(fe8_host_shader_name(invalid[i]), "Off") == 0);
            fe8_host_shader_default_config(invalid[i], &config);
            assert_config_equal(&config, &defaults[FE8_HOST_SHADER_OFF]);
            fe8_host_shader_get_config(invalid[i], &config);
            assert_config_equal(&config, &defaults[FE8_HOST_SHADER_OFF]);
            fe8_host_shader_set_config(invalid[i], &defaults[FE8_HOST_SHADER_CRT]);
        }
    }
    /* Off is immutable, and callers may safely omit an output/config pointer. */
    fe8_host_shader_set_config(FE8_HOST_SHADER_OFF, &defaults[FE8_HOST_SHADER_CRT]);
    fe8_host_shader_get_config(FE8_HOST_SHADER_OFF, &config);
    assert_config_equal(&config, &defaults[FE8_HOST_SHADER_OFF]);
    fe8_host_shader_default_config(FE8_HOST_SHADER_CRT, NULL);
    fe8_host_shader_get_config(FE8_HOST_SHADER_CRT, NULL);
    fe8_host_shader_set_config(FE8_HOST_SHADER_CRT, NULL);

    /* Editing a runtime config must not change the preset used by Reset. */
    config = (Fe8HostShaderConfig){-1.0f, 2.0f, NAN, INFINITY, 1.0f, 2.0f};
    fe8_host_shader_set_config(FE8_HOST_SHADER_CRT, &config);
    fe8_host_shader_get_config(FE8_HOST_SHADER_CRT, &config);
    {
        const Fe8HostShaderConfig sanitized = {0.0f, 1.0f, 0.0f, 0.0f, 0.25f, 1.5f};
        assert_config_equal(&config, &sanitized);
    }
    fe8_host_shader_default_config(FE8_HOST_SHADER_CRT, &config);
    assert_config_equal(&config, &defaults[FE8_HOST_SHADER_CRT]);
    fe8_host_shader_set_config(FE8_HOST_SHADER_CRT, &config);
}

int main(void) {
    Fe8HostSettings settings;
    fe8_host_settings_init(&settings);
    test_shader_presets();
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

    assert(fe8_host_hotkey_for_scancode(&settings, SDL_SCANCODE_SPACE) ==
        (UINT32_C(1) << FE8_HOST_HOTKEY_SPEED_UP));
    settings.hotkeys[FE8_HOST_HOTKEY_QUICK_LOAD] = SDL_SCANCODE_SPACE;
    assert(fe8_host_hotkey_for_scancode(&settings, SDL_SCANCODE_SPACE) ==
        ((UINT32_C(1) << FE8_HOST_HOTKEY_SPEED_UP) |
         (UINT32_C(1) << FE8_HOST_HOTKEY_QUICK_LOAD)));
    assert(fe8_host_hotkey_name(FE8_HOST_HOTKEY_QUICK_SAVE)[0] == 'Q');
    return 0;
}
