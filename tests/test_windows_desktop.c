/* Exercise real Win32 controls without showing windows, sending desktop input,
   launching games, or touching the user's settings. */
#include <assert.h>
#include "../app/src/windows_library.c"

static int saved, loaded;
static int save_state(void *context, const char *path) {
    assert(context == &saved && !strcmp(path, "test-state.ss"));
    ++saved; return 1;
}
static int load_state(void *context, const char *path) {
    assert(context == &saved && !strcmp(path, "test-state.ss"));
    ++loaded; return 1;
}

int main(void) {
    wchar_t sandbox[128];
    swprintf(sandbox, 128, L"Software\\FE8DesktopTest-%lu", GetCurrentProcessId());
    HKEY isolated;
    assert(RegCreateKeyExW(HKEY_CURRENT_USER, sandbox, 0, NULL, 0, KEY_ALL_ACCESS,
        NULL, &isolated, NULL) == ERROR_SUCCESS);
    assert(RegOverridePredefKey(HKEY_CURRENT_USER, isolated) == ERROR_SUCCESS);
    INITCOMMONCONTROLSEX controls = {sizeof(controls), ICC_LISTVIEW_CLASSES};
    assert(InitCommonControlsEx(&controls));
    WNDCLASSW wc = {0}; wc.lpfnWndProc = library_proc;
    wc.hInstance = GetModuleHandleW(NULL); wc.lpszClassName = L"FE8HiddenLibraryTest";
    assert(RegisterClassW(&wc));
    Library library = {0}; fe8_host_settings_init(&library.settings);
    HWND window = CreateWindowW(wc.lpszClassName, L"Hidden test", WS_OVERLAPPEDWINDOW,
        0, 0, 960, 560, NULL, NULL, wc.hInstance, &library);
    assert(window && !IsWindowVisible(window));
    assert(GetMenuItemCount(GetMenu(window)) == 3);
    assert(!IsWindowEnabled(library.buttons[1]));

    wchar_t temp[MAX_PATH], rom[MAX_PATH];
    assert(GetTempPathW(MAX_PATH, temp));
    swprintf(rom, MAX_PATH, L"%lsfe8-desktop-%lu.gba", temp, GetCurrentProcessId());
    FILE *file = _wfopen(rom, L"wb"); assert(file);
    fputs("original test fixture", file); fclose(file);
    import_rom(window, &library, rom);
    assert(library.count == 1 && ListView_GetItemCount(library.list) == 1);
    assert(selection(&library) == 0 && IsWindowEnabled(library.buttons[1]));
    import_rom(window, &library, rom); assert(library.count == 1);
    Library restored = {0}; load_library(&restored);
    assert(restored.count == 1 && !wcscmp(restored.paths[0], rom));
    free(restored.paths[0]);
    SendMessageW(window, WM_COMMAND, REMOVE, 0);
    assert(library.count == 0 && ListView_GetItemCount(library.list) == 0);
    assert(GetFileAttributesW(rom) != INVALID_FILE_ATTRIBUTES);
    assert(!IsWindowEnabled(library.buttons[1]));
    assert(DeleteFileW(rom));

    HMENU old = GetMenu(window);
    assert(SetMenu(window, fe8_windows_menu(1))); DestroyMenu(old);
    HMENU menu = GetMenu(window);
    assert(GetMenuItemCount(menu) == 5);
    fe8_macos_install_settings_menu(&library.settings, &saved, save_state, load_state, "test-state.ss");
    fe8_windows_update_menu(window, &library.settings);
    HMENU settings = GetSubMenu(menu, 2);
    UINT audio = GetMenuItemID(settings, 0);
    int initial_audio = library.settings.audio_enabled;
    SendMessageW(window, WM_COMMAND, audio, 0);
    assert(library.settings.audio_enabled == !initial_audio);
    assert(!!(GetMenuState(menu, audio, MF_BYCOMMAND) & MF_CHECKED) == !initial_audio);
    Fe8HostSettings loaded_settings; fe8_host_settings_init(&loaded_settings);
    fe8_macos_load_settings(&loaded_settings);
    assert(loaded_settings.audio_enabled == !initial_audio);
    HMENU bindings = GetSubMenu(settings, 6);
    wchar_t label[160];
    assert(GetMenuStringW(bindings, 0, label, 160, MF_BYPOSITION));
    assert(wcsstr(label, L"Z"));
    assert(fe8_windows_menu_command(window, FE8_WIN_SAVE, &library.settings));
    assert(fe8_windows_menu_command(window, FE8_WIN_LOAD, &library.settings));
    assert(saved == 1 && loaded == 1);
    fe8_macos_install_settings_menu(&library.settings, NULL, NULL, NULL, NULL);
    fe8_windows_update_menu(window, &library.settings);
    assert(GetMenuState(menu, FE8_WIN_SAVE, MF_BYCOMMAND) & MF_GRAYED);
    assert(GetMenuState(menu, FE8_WIN_LOAD, MF_BYCOMMAND) & MF_GRAYED);
    assert(!IsWindowVisible(window)); DestroyWindow(window);
    assert(RegOverridePredefKey(HKEY_CURRENT_USER, NULL) == ERROR_SUCCESS);
    RegCloseKey(isolated);
    assert(RegDeleteTreeW(HKEY_CURRENT_USER, sandbox) == ERROR_SUCCESS);
    puts("Hidden Windows desktop: library persistence/removal, menus, settings and state callbacks passed");
    return 0;
}
