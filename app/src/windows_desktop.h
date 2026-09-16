#ifndef FE8_WINDOWS_DESKTOP_H
#define FE8_WINDOWS_DESKTOP_H
#include <windows.h>
#include <SDL.h>
#include "host_settings.h"
enum { FE8_WIN_OPEN = 1001, FE8_WIN_LIBRARY, FE8_WIN_EXIT, FE8_WIN_ARMORY,
    FE8_WIN_SAVE = 1101, FE8_WIN_LOAD, FE8_WIN_SAVE_AS, FE8_WIN_LOAD_FROM };

int fe8_windows_library(void);
void fe8_windows_attach_menu(SDL_Window *window);
HMENU fe8_windows_menu(int in_game);
int fe8_windows_menu_command(HWND window, unsigned command, Fe8HostSettings *settings);
void fe8_windows_update_menu(HWND window, const Fe8HostSettings *settings);
void fe8_windows_spawn_game(HWND owner, const wchar_t *rom, int resume);
char *fe8_windows_choose_rom(HWND owner, int *cancelled);
wchar_t *fe8_windows_wide(const char *text);
char *fe8_windows_utf8(const wchar_t *text);
char *fe8_windows_save_directory(const char *rom);
#endif
