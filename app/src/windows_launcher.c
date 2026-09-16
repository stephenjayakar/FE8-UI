#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commdlg.h>
#include <SDL.h>
#include <mgba-util/sha1.h>
#include <mgba-util/vfs.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "windows_launcher.h"
#include "windows_desktop.h"

wchar_t *fe8_windows_wide(const char *path) {
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
    wchar_t *wide = count ? malloc((size_t)count * sizeof(*wide)) : NULL;
    if (wide && !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, count)) {
        free(wide);
        wide = NULL;
    }
    return wide;
}

static void show_error(const wchar_t *message) {
    MessageBoxW(NULL, message, L"FE8 Extended Frontend", MB_OK | MB_ICONERROR);
}

char *fe8_windows_utf8(const wchar_t *text) {
    int count = WideCharToMultiByte(CP_UTF8, 0, text, -1, NULL, 0, NULL, NULL);
    char *utf8 = count ? malloc((size_t)count) : NULL;
    if (utf8) WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8, count, NULL, NULL);
    return utf8;
}

char *fe8_windows_choose_rom(HWND owner, int *cancelled) {
    wchar_t *path = calloc(32768, sizeof(*path));
    OPENFILENAMEW dialog = {0};
    char *utf8 = NULL;
    *cancelled = 0;
    if (!path) return NULL;
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrTitle = L"Open a Fire Emblem ROM";
    dialog.lpstrFilter = L"Game Boy Advance ROMs (*.gba)\0*.gba\0All files (*.*)\0*.*\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = 32768;
    dialog.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&dialog)) {
        int count = WideCharToMultiByte(CP_UTF8, 0, path, -1, NULL, 0, NULL, NULL);
        utf8 = count ? malloc((size_t)count) : NULL;
        if (utf8 && !WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8, count, NULL, NULL)) {
            free(utf8);
            utf8 = NULL;
        }
    } else {
        *cancelled = CommDlgExtendedError() == 0;
    }
    if (!utf8 && !*cancelled)
        MessageBoxW(owner, L"The ROM picker could not open or read the selected path. Please try again.",
            L"Open ROM", MB_OK | MB_ICONERROR);
    free(path);
    return utf8;
}

static char *in_directory(const char *directory, const char *name) {
    size_t size = strlen(directory) + strlen(name) + 1;
    char *path = malloc(size);
    if (path) snprintf(path, size, "%s%s", directory, name);
    return path;
}

char *fe8_windows_save_directory(const char *rom) {
    uint8_t digest[20];
    struct VFile *file = VFileOpen(rom, O_RDONLY);
    int hashed = file && sha1File(file, digest);
    if (file) file->close(file);
    if (!hashed) return NULL;
    char hash[41];
    for (int i = 0; i < 20; ++i) snprintf(hash + i * 2, 3, "%02x", digest[i]);
    return SDL_GetPrefPath("FE8 Extended Frontend", hash);
}

int fe8_windows_launch(int argc, char **argv, int (*run_game)(int, char **)) {
    /* Preserve the diagnostic CLI. Explorer also passes a dropped file as a
       single positional argument, so it shares the library launch path. */
    int desktop = (argc == 3 || argc == 4) && !strcmp(argv[1], "--desktop-rom");
    int resume = desktop && argc == 4 && !strcmp(argv[3], "--resume");
    if (argc != 1 && !desktop && !(argc == 2 && argv[1][0] != '-'))
        return run_game(argc, argv);

    DWORD console_processes[2];
    if (GetConsoleProcessList(console_processes, 2) == 1)
        ShowWindow(GetConsoleWindow(), SW_HIDE);

    if (argc == 1) return fe8_windows_library();
    int result = EXIT_FAILURE;
    const char *rom = desktop ? argv[2] : argv[1];
    char *directory = NULL, *save = NULL, *state = NULL, *log = NULL;
    wchar_t *wide_log = NULL;
    if (!rom) {
        show_error(L"The ROM picker could not open. Please try again.");
        goto cleanup;
    }

    directory = fe8_windows_save_directory(rom);
    if (!directory) {
        show_error(L"The ROM could not be read or its save folder could not be created. Check the ROM path and that AppData is writable.");
        goto cleanup;
    }
    /* SDL creates a writable per-ROM folder under the user's AppData.
       No saves or logs are written beside the ROM or executable. */
    save = in_directory(directory, "cartridge.sav");
    state = in_directory(directory, "quick-state.ss");
    log = in_directory(directory, "startup.log");
    wide_log = log ? fe8_windows_wide(log) : NULL;
    if (!save || !state || !wide_log || !_wfreopen(wide_log, L"w", stderr)) {
        show_error(L"The startup log could not be opened in the save folder.");
        goto cleanup;
    }
    /* Import a sibling save once; never replace an existing isolated save. */
    wchar_t *wide_save = fe8_windows_wide(save);
    wchar_t *sibling = fe8_windows_wide(rom);
    if (wide_save && sibling && GetFileAttributesW(wide_save) == INVALID_FILE_ATTRIBUTES) {
        wchar_t *extension = wcsrchr(sibling, L'.');
        if (extension && !_wcsicmp(extension, L".gba")) {
            wcscpy(extension, L".sav");
            CopyFileW(sibling, wide_save, TRUE);
        }
    }
    free(sibling);
    free(wide_save);
    char *game_argv[] = {argv[0], "--rom", (char *)rom, "--save", save,
        "--quick-state", state, "--state", state, NULL};
    if (!resume) game_argv[7] = NULL;
    result = run_game(resume ? 9 : 7, game_argv);
    fflush(stderr);
    if (result != EXIT_SUCCESS) {
        const wchar_t *prefix = L"The game could not start. Choose a valid, extracted GBA ROM.\n\nDetails were saved to:\n";
        size_t count = wcslen(prefix) + wcslen(wide_log) + 1;
        wchar_t *message = malloc(count * sizeof(*message));
        if (message) {
            swprintf(message, count, L"%ls%ls", prefix, wide_log);
            show_error(message);
            free(message);
        } else show_error(L"The game could not start. See startup.log in the game's AppData folder.");
    }
cleanup:
    free(wide_log);
    free(log);
    free(state);
    free(save);
    SDL_free(directory);
    return result;
}
