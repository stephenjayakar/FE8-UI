#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <SDL_syswm.h>
#include <stdlib.h>
#include <stdio.h>
#include "windows_desktop.h"
#include "macos_settings.h"

static const wchar_t *settings_key = L"Software\\FE8 Extended Frontend";
enum { AUDIO = 1200, VSYNC, EXTENDED, MOUSE, BINDING = 1400, HOTKEY = 1500,
    SPEED = 1600, ZOOM = 1700, ABOUT = 1900 };
static Fe8HostSettings *game_settings;
static void *game_context;
static Fe8HostStateCallback game_save, game_load;
static const char *quick_path;

static int preference(const wchar_t *name, int fallback, int low, int high) {
    DWORD value, size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, settings_key, name, RRF_RT_REG_DWORD,
            NULL, &value, &size) != ERROR_SUCCESS || value < (DWORD)low || value > (DWORD)high)
        return fallback;
    return (int)value;
}
static void store(const wchar_t *name, int value) {
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, settings_key, 0, NULL, 0, KEY_SET_VALUE,
            NULL, &key, NULL) == ERROR_SUCCESS) {
        DWORD data = (DWORD)value;
        RegSetValueExW(key, name, 0, REG_DWORD, (const BYTE *)&data, sizeof(data));
        RegCloseKey(key);
    }
}
static void save_preferences(const Fe8HostSettings *s) {
    store(L"Audio", s->audio_enabled); store(L"VSync", s->vsync_enabled);
    store(L"Extended", s->extensions_enabled); store(L"Mouse", s->mouse_enabled);
    store(L"Speed", s->speedup_rate); store(L"Zoom", (int)(s->zoom_sensitivity * 10000 + .5));
    wchar_t key[32];
    for (int i = 0; i < FE8_HOST_BUTTON_COUNT; ++i) {
        swprintf(key, 32, L"Binding%d", i); store(key, s->bindings[i]);
    }
    for (int i = 0; i < FE8_HOST_HOTKEY_COUNT; ++i) {
        swprintf(key, 32, L"Hotkey%d", i); store(key, s->hotkeys[i]);
    }
}
void fe8_macos_load_settings(Fe8HostSettings *s) {
    s->audio_enabled = preference(L"Audio", s->audio_enabled, 0, 1);
    s->vsync_enabled = preference(L"VSync", s->vsync_enabled, 0, 1);
    s->extensions_enabled = preference(L"Extended", s->extensions_enabled, 0, 1);
    s->mouse_enabled = preference(L"Mouse", s->mouse_enabled, 0, 1);
    s->speedup_rate = preference(L"Speed", s->speedup_rate, 0, FE8_HOST_SPEEDUP_COUNT - 1);
    s->zoom_sensitivity = preference(L"Zoom", 50, 50, 300) / 10000.0;
    wchar_t key[32];
    for (int i = 0; i < FE8_HOST_BUTTON_COUNT; ++i) {
        swprintf(key, 32, L"Binding%d", i);
        s->bindings[i] = preference(key, s->bindings[i], 1, SDL_NUM_SCANCODES - 1);
    }
    for (int i = 0; i < FE8_HOST_HOTKEY_COUNT; ++i) {
        swprintf(key, 32, L"Hotkey%d", i);
        s->hotkeys[i] = preference(key, s->hotkeys[i], 1, SDL_NUM_SCANCODES - 1);
    }
    ++s->revision;
}
void fe8_macos_toggle_extensions(Fe8HostSettings *s) {
    fe8_host_toggle_extensions(s);
    store(L"Extended", s->extensions_enabled);
}
void fe8_macos_install_settings_menu(Fe8HostSettings *settings, void *context,
    Fe8HostStateCallback save, Fe8HostStateCallback load, const char *path) {
    game_settings = settings; game_context = context;
    game_save = save; game_load = load; quick_path = path;
}

HMENU fe8_windows_menu(int in_game) {
    HMENU bar = CreateMenu(), file = CreatePopupMenu(), state = CreatePopupMenu();
    HMENU settings = CreatePopupMenu(), controls = CreatePopupMenu();
    HMENU hotkeys = CreatePopupMenu(), speed = CreatePopupMenu(), zoom = CreatePopupMenu();
    AppendMenuW(file, MF_STRING, FE8_WIN_OPEN, in_game ? L"Open ROM..." : L"Add ROM...");
    if (in_game) AppendMenuW(file, MF_STRING, FE8_WIN_LIBRARY, L"ROM Library...");
    AppendMenuW(file, MF_SEPARATOR, 0, NULL);
    AppendMenuW(file, MF_STRING, FE8_WIN_EXIT, in_game ? L"Close game" : L"Exit");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)file, L"&File");
    if (in_game) {
        AppendMenuW(state, MF_STRING, FE8_WIN_SAVE, L"Quick save");
        AppendMenuW(state, MF_STRING, FE8_WIN_LOAD, L"Quick load");
        AppendMenuW(state, MF_SEPARATOR, 0, NULL);
        AppendMenuW(state, MF_STRING, FE8_WIN_SAVE_AS, L"Save state as...");
        AppendMenuW(state, MF_STRING, FE8_WIN_LOAD_FROM, L"Load state...");
        AppendMenuW(bar, MF_POPUP, (UINT_PTR)state, L"&State");
    } else DestroyMenu(state);
    AppendMenuW(settings, MF_STRING, AUDIO, L"Audio");
    AppendMenuW(settings, MF_STRING, VSYNC, L"VSync");
    AppendMenuW(settings, MF_STRING, EXTENDED, L"Extended rendering");
    AppendMenuW(settings, MF_STRING, MOUSE, L"Mouse controls");
    for (int i = 0; i < FE8_HOST_SPEEDUP_COUNT; ++i) {
        wchar_t *name = fe8_windows_wide(fe8_host_speedup_name(i));
        AppendMenuW(speed, MF_STRING, SPEED + i, name); free(name);
    }
    AppendMenuW(zoom, MF_STRING, ZOOM, L"Low (0.5%)");
    AppendMenuW(zoom, MF_STRING, ZOOM + 1, L"Medium (1.5%)");
    AppendMenuW(zoom, MF_STRING, ZOOM + 2, L"High (3%)");
    AppendMenuW(settings, MF_POPUP, (UINT_PTR)speed, L"Fast-forward speed");
    AppendMenuW(settings, MF_POPUP, (UINT_PTR)zoom, L"Zoom sensitivity");
    for (int i = 0; i < FE8_HOST_BUTTON_COUNT; ++i) AppendMenuW(controls, MF_STRING, BINDING + i, L"Binding");
    for (int i = 0; i < FE8_HOST_HOTKEY_COUNT; ++i) AppendMenuW(hotkeys, MF_STRING, HOTKEY + i, L"Hotkey");
    AppendMenuW(controls, MF_SEPARATOR, 0, NULL);
    AppendMenuW(controls, MF_POPUP, (UINT_PTR)hotkeys, L"Hotkeys");
    AppendMenuW(settings, MF_POPUP, (UINT_PTR)controls, L"Keyboard controls...");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)settings, L"&Settings");
    if (in_game) AppendMenuW(bar, MF_STRING, FE8_WIN_ARMORY, L"&Armory");
    AppendMenuW(bar, MF_STRING, ABOUT, L"&Help");
    return bar;
}
void fe8_windows_update_menu(HWND window, const Fe8HostSettings *s) {
    HMENU menu = GetMenu(window);
    if (!menu || !s) return;
    int values[] = {s->audio_enabled, s->vsync_enabled, s->extensions_enabled, s->mouse_enabled};
    for (int i = 0; i < 4; ++i) CheckMenuItem(menu, AUDIO + i, MF_BYCOMMAND | (values[i] ? MF_CHECKED : MF_UNCHECKED));
    for (int i = 0; i < FE8_HOST_SPEEDUP_COUNT; ++i)
        CheckMenuItem(menu, SPEED + i, MF_BYCOMMAND | (i == s->speedup_rate ? MF_CHECKED : MF_UNCHECKED));
    double sensitivity[] = {.005, .015, .030};
    for (int i = 0; i < 3; ++i)
        CheckMenuItem(menu, ZOOM + i, MF_BYCOMMAND | (s->zoom_sensitivity == sensitivity[i] ? MF_CHECKED : MF_UNCHECKED));
    for (int group = 0; group < 2; ++group) {
        int count = group ? FE8_HOST_HOTKEY_COUNT : FE8_HOST_BUTTON_COUNT;
        for (int i = 0; i < count; ++i) {
            const char *name = group ? fe8_host_hotkey_name(i) : fe8_host_button_name(i);
            SDL_Scancode key = group ? s->hotkeys[i] : s->bindings[i];
            char label[160]; snprintf(label, sizeof(label), "%s: %s...", name, SDL_GetScancodeName(key));
            wchar_t *wide = fe8_windows_wide(label);
            ModifyMenuW(menu, (group ? HOTKEY : BINDING) + i, MF_BYCOMMAND | MF_STRING,
                (group ? HOTKEY : BINDING) + i, wide); free(wide);
        }
    }
    EnableMenuItem(menu, FE8_WIN_SAVE, MF_BYCOMMAND | (quick_path ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(menu, FE8_WIN_LOAD, MF_BYCOMMAND | (quick_path ? MF_ENABLED : MF_GRAYED));
}

static SDL_Scancode scancode(WPARAM key, LPARAM detail) {
    if (key >= 'A' && key <= 'Z') return SDL_SCANCODE_A + (int)key - 'A';
    if (key >= '1' && key <= '9') return SDL_SCANCODE_1 + (int)key - '1';
    if (key >= VK_F1 && key <= VK_F12) return SDL_SCANCODE_F1 + (int)key - VK_F1;
    if (key >= VK_NUMPAD1 && key <= VK_NUMPAD9) return SDL_SCANCODE_KP_1 + (int)key - VK_NUMPAD1;
    if (key == VK_SHIFT) return ((detail >> 16) & 255) == 0x36 ? SDL_SCANCODE_RSHIFT : SDL_SCANCODE_LSHIFT;
    if (key == VK_CONTROL) return (detail & (1 << 24)) ? SDL_SCANCODE_RCTRL : SDL_SCANCODE_LCTRL;
    if (key == VK_MENU) return (detail & (1 << 24)) ? SDL_SCANCODE_RALT : SDL_SCANCODE_LALT;
    switch (key) {
    case '0': return SDL_SCANCODE_0;
    case VK_RETURN: return (detail & (1 << 24)) ? SDL_SCANCODE_KP_ENTER : SDL_SCANCODE_RETURN;
    case VK_NUMPAD0: return SDL_SCANCODE_KP_0; case VK_DECIMAL: return SDL_SCANCODE_KP_PERIOD;
    case VK_ADD: return SDL_SCANCODE_KP_PLUS; case VK_SUBTRACT: return SDL_SCANCODE_KP_MINUS;
    case VK_MULTIPLY: return SDL_SCANCODE_KP_MULTIPLY; case VK_DIVIDE: return SDL_SCANCODE_KP_DIVIDE;
    case VK_LWIN: return SDL_SCANCODE_LGUI; case VK_RWIN: return SDL_SCANCODE_RGUI;
    case VK_CAPITAL: return SDL_SCANCODE_CAPSLOCK;
    case VK_BACK: return SDL_SCANCODE_BACKSPACE; case VK_SPACE: return SDL_SCANCODE_SPACE;
    case VK_TAB: return SDL_SCANCODE_TAB; case VK_UP: return SDL_SCANCODE_UP;
    case VK_DOWN: return SDL_SCANCODE_DOWN; case VK_LEFT: return SDL_SCANCODE_LEFT;
    case VK_RIGHT: return SDL_SCANCODE_RIGHT; case VK_HOME: return SDL_SCANCODE_HOME;
    case VK_END: return SDL_SCANCODE_END; case VK_INSERT: return SDL_SCANCODE_INSERT;
    case VK_DELETE: return SDL_SCANCODE_DELETE; case VK_PRIOR: return SDL_SCANCODE_PAGEUP;
    case VK_NEXT: return SDL_SCANCODE_PAGEDOWN; case VK_OEM_PLUS: return SDL_SCANCODE_EQUALS;
    case VK_OEM_MINUS: return SDL_SCANCODE_MINUS; case VK_OEM_COMMA: return SDL_SCANCODE_COMMA;
    case VK_OEM_PERIOD: return SDL_SCANCODE_PERIOD; case VK_OEM_2: return SDL_SCANCODE_SLASH;
    case VK_OEM_1: return SDL_SCANCODE_SEMICOLON; case VK_OEM_3: return SDL_SCANCODE_GRAVE;
    case VK_OEM_4: return SDL_SCANCODE_LEFTBRACKET; case VK_OEM_5: return SDL_SCANCODE_BACKSLASH;
    case VK_OEM_6: return SDL_SCANCODE_RIGHTBRACKET; case VK_OEM_7: return SDL_SCANCODE_APOSTROPHE;
    default: return SDL_SCANCODE_UNKNOWN;
    }
}
typedef struct Capture { int done; SDL_Scancode key; } Capture;
static LRESULT CALLBACK capture_proc(HWND window, UINT msg, WPARAM wp, LPARAM lp) {
    Capture *capture = (Capture *)GetWindowLongPtrW(window, GWLP_USERDATA);
    if (msg == WM_CREATE) {
        capture = ((CREATESTRUCTW *)lp)->lpCreateParams;
        SetWindowLongPtrW(window, GWLP_USERDATA, (LONG_PTR)capture);
        HWND label = CreateWindowW(L"STATIC", L"Press a key to assign it.\nEscape cancels without changes.",
            WS_CHILD | WS_VISIBLE, 20, 20, 340, 65, window, NULL, NULL, NULL);
        SendMessageW(label, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
        return 0;
    }
    if (capture && (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN)) {
        capture->key = wp == VK_ESCAPE ? SDL_SCANCODE_UNKNOWN : scancode(wp, lp);
        if (capture->key || wp == VK_ESCAPE) DestroyWindow(window);
        return 0;
    }
    if (msg == WM_DESTROY && capture) { capture->done = 1; return 0; }
    return DefWindowProcW(window, msg, wp, lp);
}
static SDL_Scancode capture_key(HWND owner) {
    WNDCLASSW wc = {0}; wc.lpfnWndProc = capture_proc; wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"FE8KeyBinding"; wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1); RegisterClassW(&wc);
    Capture capture = {0}; RECT bounds; GetWindowRect(owner, &bounds);
    HWND dialog = CreateWindowExW(WS_EX_DLGMODALFRAME, wc.lpszClassName, L"Keyboard binding",
        WS_CAPTION | WS_SYSMENU, bounds.left + 60, bounds.top + 80, 400, 140,
        owner, NULL, wc.hInstance, &capture);
    if (!dialog) return SDL_SCANCODE_UNKNOWN;
    EnableWindow(owner, FALSE); ShowWindow(dialog, SW_SHOW); SetFocus(dialog);
    MSG message;
    while (!capture.done && GetMessageW(&message, NULL, 0, 0) > 0) {
        TranslateMessage(&message); DispatchMessageW(&message);
    }
    EnableWindow(owner, TRUE); SetForegroundWindow(owner);
    return capture.key;
}
static void state_action(HWND owner, int save, int choose) {
    wchar_t *buffer = NULL; char *selected = NULL;
    const char *path = quick_path;
    if (choose) {
        buffer = calloc(32768, sizeof(*buffer)); if (!buffer) return;
        OPENFILENAMEW dialog = {0}; dialog.lStructSize = sizeof(dialog); dialog.hwndOwner = owner;
        dialog.lpstrFile = buffer; dialog.nMaxFile = 32768;
        dialog.lpstrFilter = L"mGBA save state (*.ss)\0*.ss\0All files\0*.*\0";
        dialog.lpstrDefExt = L"ss"; dialog.lpstrTitle = save ? L"Save state" : L"Load state";
        dialog.Flags = OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
        if (!(save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog))) { free(buffer); return; }
        path = selected = fe8_windows_utf8(buffer);
    }
    Fe8HostStateCallback action = save ? game_save : game_load;
    if (!path || !action || !action(game_context, path))
        MessageBoxW(owner, save ? L"The state could not be saved." : L"The state could not be loaded. Save a state first, or choose an existing state file.",
            L"FE8 save state", MB_OK | MB_ICONERROR);
    free(buffer); free(selected);
}
int fe8_windows_menu_command(HWND window, unsigned command, Fe8HostSettings *s) {
    int changed = 1;
    switch (command) {
    case AUDIO: s->audio_enabled = !s->audio_enabled; break;
    case VSYNC: s->vsync_enabled = !s->vsync_enabled; break;
    case EXTENDED: s->extensions_enabled = !s->extensions_enabled; break;
    case MOUSE: s->mouse_enabled = !s->mouse_enabled; break;
    case FE8_WIN_EXIT: SendMessageW(window, WM_CLOSE, 0, 0); return 1;
    case FE8_WIN_LIBRARY: fe8_windows_spawn_game(window, NULL, 0); return 1;
    case FE8_WIN_OPEN: {
        int cancelled; char *rom = fe8_windows_choose_rom(window, &cancelled);
        wchar_t *wide = rom ? fe8_windows_wide(rom) : NULL;
        if (wide) fe8_windows_spawn_game(window, wide, 0);
        free(wide); free(rom); return 1;
    }
    case FE8_WIN_ARMORY: {
        SDL_Event event = {0}; event.type = SDL_KEYDOWN; event.key.state = SDL_PRESSED;
        event.key.keysym.scancode = SDL_SCANCODE_I; event.key.keysym.sym = SDLK_i;
        SDL_PushEvent(&event); event.type = SDL_KEYUP; event.key.state = SDL_RELEASED; SDL_PushEvent(&event);
        return 1;
    }
    case FE8_WIN_SAVE: state_action(window, 1, 0); return 1;
    case FE8_WIN_LOAD: state_action(window, 0, 0); return 1;
    case FE8_WIN_SAVE_AS: state_action(window, 1, 1); return 1;
    case FE8_WIN_LOAD_FROM: state_action(window, 0, 1); return 1;
    case ABOUT:
        MessageBoxW(window, L"FE8 Extended Frontend\n\nAdd a .gba ROM to the library, then choose Play.\nResume State loads that game's last quick save.\n\nIn game: I opens Armory, F5 saves a state, F8 loads it,\nF6 toggles extended rendering. Settings lets you rebind keys.\n\nWindows uses the SDL renderer; CRT shader presets are not available.",
            L"FE8 help", MB_OK); return 1;
    default:
        if (command >= SPEED && command < SPEED + FE8_HOST_SPEEDUP_COUNT) s->speedup_rate = command - SPEED;
        else if (command >= ZOOM && command < ZOOM + 3) {
            double values[] = {.005, .015, .030}; s->zoom_sensitivity = values[command - ZOOM];
        } else if ((command >= BINDING && command < BINDING + FE8_HOST_BUTTON_COUNT) ||
                (command >= HOTKEY && command < HOTKEY + FE8_HOST_HOTKEY_COUNT)) {
            SDL_Scancode key = capture_key(window);
            if (key != SDL_SCANCODE_UNKNOWN) {
                if (command >= HOTKEY) s->hotkeys[command - HOTKEY] = key;
                else s->bindings[command - BINDING] = key;
            } else changed = 0;
        } else return 0;
    }
    if (changed) { ++s->revision; save_preferences(s); }
    fe8_windows_update_menu(window, s);
    return 1;
}
static LRESULT CALLBACK game_proc(HWND window, UINT message, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR data) {
    (void)data;
    if (message == WM_INITMENUPOPUP) fe8_windows_update_menu(window, game_settings);
    if (message == WM_COMMAND && game_settings && fe8_windows_menu_command(window, LOWORD(wp), game_settings)) return 0;
    if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(window, game_proc, id); game_settings = NULL; quick_path = NULL;
        game_context = NULL; game_save = game_load = NULL;
    }
    return DefSubclassProc(window, message, wp, lp);
}
void fe8_windows_attach_menu(SDL_Window *window) {
    SDL_SysWMinfo info; SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(window, &info)) return;
    HWND native = info.info.win.window;
    SetMenu(native, fe8_windows_menu(1));
    SetWindowSubclass(native, game_proc, 1, 0);
    fe8_windows_update_menu(native, game_settings);
    DrawMenuBar(native);
    SetWindowPos(native, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
}
