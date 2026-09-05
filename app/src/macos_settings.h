#ifndef FE8_MACOS_SETTINGS_H
#define FE8_MACOS_SETTINGS_H

#include "host_settings.h"

typedef int (*Fe8HostStateCallback)(void *context, const char *path);

void fe8_macos_load_settings(Fe8HostSettings *settings);
/* Shared by the hotkey and native controls; persists/synchronizes on macOS. */
void fe8_macos_set_extensions_enabled(Fe8HostSettings *settings, int enabled);
void fe8_macos_install_settings_menu(
    Fe8HostSettings *settings,
    void *state_context,
    Fe8HostStateCallback save_state,
    Fe8HostStateCallback load_state,
    const char *quick_state_path);

#endif
