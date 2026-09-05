#include "macos_settings.h"

void fe8_macos_set_extensions_enabled(Fe8HostSettings *settings, int enabled) {
    fe8_host_set_extensions_enabled(settings, enabled);
}

void fe8_macos_load_settings(Fe8HostSettings *settings) {
    (void)settings;
}

void fe8_macos_install_settings_menu(
    Fe8HostSettings *settings,
    void *state_context,
    Fe8HostStateCallback save_state,
    Fe8HostStateCallback load_state,
    const char *quick_state_path) {
    (void)settings;
    (void)state_context;
    (void)save_state;
    (void)load_state;
    (void)quick_state_path;
}
