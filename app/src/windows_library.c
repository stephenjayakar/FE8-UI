#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <stdlib.h>
#include <stdio.h>
#include "windows_desktop.h"
#include "macos_settings.h"

#ifdef _MSC_VER
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif

enum { ADD = 201, PLAY, RESUME, REMOVE, FOLDER, LIST, MAX_GAMES = 128 };
typedef struct Library {
    HWND list, explanation, buttons[5];
    wchar_t *paths[MAX_GAMES];
    int count;
    Fe8HostSettings settings;
} Library;
static const wchar_t *library_key = L"Software\\FE8 Extended Frontend\\Library";

void fe8_windows_spawn_game(HWND owner, const wchar_t *rom, int resume) {
    wchar_t *exe = calloc(32768, sizeof(*exe));
    if (!exe) return;
    DWORD length = GetModuleFileNameW(NULL, exe, 32768);
    size_t size = length + (rom ? wcslen(rom) : 0) + 64;
    wchar_t *command = calloc(size, sizeof(*command));
    if (!length || length >= 32768 || !command) { free(command); free(exe); return; }
    if (rom) swprintf(command, size, L"\"%ls\" --desktop-rom \"%ls\"%ls", exe, rom, resume ? L" --resume" : L"");
    else swprintf(command, size, L"\"%ls\"", exe);
    STARTUPINFOW startup = {0}; PROCESS_INFORMATION process = {0}; startup.cb = sizeof(startup);
    if (CreateProcessW(exe, command, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &startup, &process)) {
        CloseHandle(process.hThread); CloseHandle(process.hProcess);
    } else MessageBoxW(owner, L"The game window could not be started. Extract the complete ZIP and try again.",
        L"FE8 Extended Frontend", MB_OK | MB_ICONERROR);
    free(command); free(exe);
}
static void save_library(Library *library) {
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, library_key, 0, NULL, 0, KEY_SET_VALUE,
            NULL, &key, NULL) != ERROR_SUCCESS) return;
    DWORD count = (DWORD)library->count;
    for (int i = 0; i < library->count; ++i) {
        wchar_t name[32]; swprintf(name, 32, L"Game%d", i);
        RegSetValueExW(key, name, 0, REG_SZ, (const BYTE *)library->paths[i],
            (DWORD)((wcslen(library->paths[i]) + 1) * sizeof(wchar_t)));
    }
    RegSetValueExW(key, L"Count", 0, REG_DWORD, (const BYTE *)&count, sizeof(count));
    RegCloseKey(key);
}
static void load_library(Library *library) {
    DWORD count = 0, size = sizeof(count);
    RegGetValueW(HKEY_CURRENT_USER, library_key, L"Count", RRF_RT_REG_DWORD, NULL, &count, &size);
    if (count > MAX_GAMES) count = MAX_GAMES;
    for (DWORD i = 0; i < count; ++i) {
        wchar_t name[32]; swprintf(name, 32, L"Game%lu", i); size = 0;
        if (RegGetValueW(HKEY_CURRENT_USER, library_key, name, RRF_RT_REG_SZ, NULL, NULL, &size) != ERROR_SUCCESS ||
                size < sizeof(wchar_t) || size > 65536) continue;
        wchar_t *path = calloc(size + sizeof(wchar_t), 1);
        if (path && RegGetValueW(HKEY_CURRENT_USER, library_key, name, RRF_RT_REG_SZ, NULL, path, &size) == ERROR_SUCCESS)
            library->paths[library->count++] = path;
        else free(path);
    }
}
static int selection(Library *library) { return ListView_GetNextItem(library->list, -1, LVNI_SELECTED); }
static void buttons(Library *library) {
    int selected = selection(library);
    for (int i = 1; i < 5; ++i) EnableWindow(library->buttons[i], selected >= 0);
}
static void refresh(Library *library, int selected) {
    ListView_DeleteAllItems(library->list);
    for (int i = 0; i < library->count; ++i) {
        wchar_t *name = wcsrchr(library->paths[i], L'\\');
        if (!name) name = wcsrchr(library->paths[i], L'/');
        LVITEMW item = {0}; item.mask = LVIF_TEXT; item.iItem = i;
        item.pszText = name ? name + 1 : library->paths[i];
        SendMessageW(library->list, LVM_INSERTITEMW, 0, (LPARAM)&item);
        item.iSubItem = 1; item.pszText = library->paths[i];
        SendMessageW(library->list, LVM_SETITEMTEXTW, i, (LPARAM)&item);
    }
    if (selected >= 0 && selected < library->count)
        ListView_SetItemState(library->list, selected, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    SetWindowTextW(library->explanation, library->count ?
        L"Select a game and choose Play, or double-click it. Resume State loads its last quick save." :
        L"Add a .gba ROM to get started. You can also drop ROM files into this window.");
    buttons(library);
}
static void import_rom(HWND window, Library *library, const wchar_t *path) {
    const wchar_t *ext = wcsrchr(path, L'.');
    if (!ext || _wcsicmp(ext, L".gba") || GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
        MessageBoxW(window, L"Choose an extracted .gba ROM file.", L"Add ROM", MB_OK | MB_ICONINFORMATION); return;
    }
    for (int i = 0; i < library->count; ++i) if (!_wcsicmp(path, library->paths[i])) { refresh(library, i); return; }
    if (library->count == MAX_GAMES) {
        MessageBoxW(window, L"The library is full. Remove a library entry before adding another ROM.", L"Add ROM", MB_OK); return;
    }
    wchar_t *copy = malloc((wcslen(path) + 1) * sizeof(*copy));
    if (!copy) return;
    wcscpy(copy, path); library->paths[library->count++] = copy;
    save_library(library); refresh(library, library->count - 1);
}
static void play(HWND window, Library *library, int resume) {
    int selected = selection(library); if (selected < 0) return;
    if (resume) {
        char *rom = fe8_windows_utf8(library->paths[selected]);
        char *folder = rom ? fe8_windows_save_directory(rom) : NULL;
        char *state = NULL; wchar_t *wide = NULL;
        if (folder) { size_t length = strlen(folder) + 32; state = malloc(length);
            if (state) { snprintf(state, length, "%squick-state.ss", folder); wide = fe8_windows_wide(state); } }
        int exists = wide && GetFileAttributesW(wide) != INVALID_FILE_ATTRIBUTES;
        free(wide); free(state); SDL_free(folder); free(rom);
        if (!exists) { MessageBoxW(window, L"No quick save exists for this game yet. Choose Play, then use State > Quick save (F5).",
            L"Resume State", MB_OK | MB_ICONINFORMATION); return; }
    }
    fe8_windows_spawn_game(window, library->paths[selected], resume);
}
static void layout(HWND window, Library *library) {
    RECT area; GetClientRect(window, &area);
    int width = area.right, height = area.bottom;
    MoveWindow(library->explanation, 16, 14, width - 32, 40, TRUE);
    MoveWindow(library->list, 16, 60, width - 32, height - 126, TRUE);
    for (int i = 0; i < 5; ++i) MoveWindow(library->buttons[i], 16 + i * 126, height - 50, 116, 32, TRUE);
    ListView_SetColumnWidth(library->list, 0, (width - 32) / 3);
    ListView_SetColumnWidth(library->list, 1, (width - 32) * 2 / 3 - 20);
}
static LRESULT CALLBACK library_proc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    Library *library = (Library *)GetWindowLongPtrW(window, GWLP_USERDATA);
    if (message == WM_CREATE) {
        library = ((CREATESTRUCTW *)lp)->lpCreateParams;
        SetWindowLongPtrW(window, GWLP_USERDATA, (LONG_PTR)library);
        HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        library->explanation = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE, 0, 0, 1, 1, window, NULL, NULL, NULL);
        library->list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"ROM library", WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
            0, 0, 1, 1, window, (HMENU)LIST, NULL, NULL);
        ListView_SetExtendedListViewStyle(library->list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        LVCOLUMNW column = {0}; column.mask = LVCF_TEXT | LVCF_WIDTH; column.cx = 250; column.pszText = L"Game";
        SendMessageW(library->list, LVM_INSERTCOLUMNW, 0, (LPARAM)&column);
        column.cx = 500; column.pszText = L"ROM location"; SendMessageW(library->list, LVM_INSERTCOLUMNW, 1, (LPARAM)&column);
        const wchar_t *labels[] = {L"Add ROM...", L"Play", L"Resume State", L"Remove entry", L"Save folder"};
        for (int i = 0; i < 5; ++i) {
            library->buttons[i] = CreateWindowW(L"BUTTON", labels[i], WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                0, 0, 1, 1, window, (HMENU)(INT_PTR)(ADD + i), NULL, NULL);
            SendMessageW(library->buttons[i], WM_SETFONT, (WPARAM)font, TRUE);
        }
        SendMessageW(library->list, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessageW(library->explanation, WM_SETFONT, (WPARAM)font, TRUE);
        SetMenu(window, fe8_windows_menu(0)); DragAcceptFiles(window, TRUE);
        load_library(library); refresh(library, library->count ? 0 : -1); layout(window, library);
        return 0;
    }
    if (!library) return DefWindowProcW(window, message, wp, lp);
    switch (message) {
    case WM_SIZE: layout(window, library); return 0;
    case WM_GETMINMAXINFO: ((MINMAXINFO *)lp)->ptMinTrackSize = (POINT){690, 380}; return 0;
    case WM_INITMENUPOPUP:
        fe8_macos_load_settings(&library->settings);
        fe8_windows_update_menu(window, &library->settings); return 0;
    case WM_DROPFILES: {
        HDROP drop = (HDROP)wp; UINT count = DragQueryFileW(drop, 0xFFFFFFFF, NULL, 0);
        for (UINT i = 0; i < count; ++i) {
            UINT length = DragQueryFileW(drop, i, NULL, 0);
            wchar_t *path = calloc((size_t)length + 1, sizeof(*path));
            if (path) { DragQueryFileW(drop, i, path, length + 1); import_rom(window, library, path); free(path); }
        }
        DragFinish(drop); return 0;
    }
    case WM_NOTIFY:
        if (((NMHDR *)lp)->idFrom == LIST) {
            if (((NMHDR *)lp)->code == LVN_ITEMCHANGED) buttons(library);
            if (((NMHDR *)lp)->code == NM_DBLCLK) play(window, library, 0);
            if (((NMHDR *)lp)->code == LVN_KEYDOWN && ((NMLVKEYDOWN *)lp)->wVKey == VK_RETURN) play(window, library, 0);
        }
        return 0;
    case WM_COMMAND: {
        unsigned command = LOWORD(wp); int selected = selection(library);
        if (command == ADD || command == FE8_WIN_OPEN) {
            int cancelled; char *rom = fe8_windows_choose_rom(window, &cancelled);
            wchar_t *wide = rom ? fe8_windows_wide(rom) : NULL;
            if (wide) import_rom(window, library, wide);
            free(wide); free(rom);
        } else if (command == PLAY || command == RESUME) play(window, library, command == RESUME);
        else if (command == REMOVE && selected >= 0) {
            free(library->paths[selected]);
            memmove(library->paths + selected, library->paths + selected + 1, (library->count - selected - 1) * sizeof(library->paths[0]));
            --library->count; save_library(library); refresh(library, library->count ? 0 : -1);
        } else if (command == FOLDER && selected >= 0) {
            char *rom = fe8_windows_utf8(library->paths[selected]);
            char *folder = rom ? fe8_windows_save_directory(rom) : NULL;
            wchar_t *wide = folder ? fe8_windows_wide(folder) : NULL;
            if (wide) ShellExecuteW(window, L"open", wide, NULL, NULL, SW_SHOWNORMAL);
            free(wide); SDL_free(folder); free(rom);
        } else fe8_windows_menu_command(window, command, &library->settings);
        return 0;
    }
    case WM_DESTROY: PostQuitMessage(0); return 0;
    default: return DefWindowProcW(window, message, wp, lp);
    }
}
int fe8_windows_library(void) {
    INITCOMMONCONTROLSEX controls = {sizeof(controls), ICC_LISTVIEW_CLASSES}; InitCommonControlsEx(&controls);
    Library *library = calloc(1, sizeof(*library)); if (!library) return 1;
    fe8_host_settings_init(&library->settings); fe8_macos_load_settings(&library->settings);
    WNDCLASSW wc = {0}; wc.lpfnWndProc = library_proc; wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"FE8ROMLibrary"; wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION); wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1); RegisterClassW(&wc);
    HWND window = CreateWindowExW(0, wc.lpszClassName, L"FE8 Extended Frontend - ROM Library",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 960, 560, NULL, NULL, wc.hInstance, library);
    if (!window) { free(library); return 1; }
    ShowWindow(window, SW_SHOW); UpdateWindow(window);
    MSG message;
    while (GetMessageW(&message, NULL, 0, 0) > 0) {
        if (message.message == WM_KEYDOWN && message.wParam == VK_RETURN &&
                message.hwnd == library->list) { play(window, library, 0); continue; }
        if (!IsDialogMessageW(window, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    for (int i = 0; i < library->count; ++i) free(library->paths[i]);
    free(library); return 0;
}
