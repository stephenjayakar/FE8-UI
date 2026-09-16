#include "windows_launcher.h"
#include <windows.h>
#include <SDL.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const char *expected_rom;
static char save_paths[2][4096];
static int calls;
static int expect_resume;

/* The library controls are exercised by the separate hidden-window test. */
int fe8_windows_library(void) { return 42; }

static int game(int argc, char **argv) {
    assert(argc == (expect_resume ? 9 : 7) && argv[argc] == NULL);
    if (expect_resume) assert(!strcmp(argv[7], "--state") && !strcmp(argv[8], argv[6]));
    assert(!strcmp(argv[1], "--rom") && !strcmp(argv[2], expected_rom));
    assert(!strcmp(argv[3], "--save") && strstr(argv[4], "cartridge.sav"));
    assert(!strcmp(argv[5], "--quick-state") && strstr(argv[6], "quick-state.ss"));
    assert(strstr(argv[4], "FE8 Extended Frontend"));
    FILE *save = fopen(argv[4], "rb"); assert(save);
    char contents[32] = {0}; assert(fread(contents, 1, sizeof(contents) - 1, save)); fclose(save);
    assert(!strcmp(contents, "original-save"));
    assert(strlen(argv[4]) < sizeof(save_paths[0]));
    strcpy(save_paths[calls++ % 2], argv[4]);
    return 0;
}

static int cli(int argc, char **argv) {
    assert(argc == 2 && !strcmp(argv[1], "--help"));
    return 37;
}

int main(void) {
    char *help[] = {"fe8", "--help", NULL};
    assert(fe8_windows_launch(2, help, cli) == 37);
    char *library[] = {"fe8", NULL};
    assert(fe8_windows_launch(1, library, cli) == 42);

    wchar_t temporary[MAX_PATH], first[MAX_PATH], second[MAX_PATH];
    assert(GetTempPathW(MAX_PATH, temporary));
    /* Unicode and spaces exercise Explorer's UTF-8 arguments and VFS paths. */
    swprintf(first, MAX_PATH, L"%lsfe8-launch-%lu-\x65e5\x672c.gba", temporary, GetCurrentProcessId());
    swprintf(second, MAX_PATH, L"%lsfe8-launch-%lu-copy.gba", temporary, GetCurrentProcessId());
    const wchar_t *files[] = {first, second};
    for (int i = 0; i < 2; ++i) {
        FILE *file = _wfopen(files[i], L"wb");
        assert(file);
        fprintf(file, "FE8 launcher fixture %lu", GetCurrentProcessId());
        assert(!fclose(file));
        wchar_t sibling[MAX_PATH]; wcscpy(sibling, files[i]);
        wcscpy(wcsrchr(sibling, L'.'), L".sav");
        file = _wfopen(sibling, L"wb"); assert(file);
        fputs(i ? "do-not-overwrite-existing-save" : "original-save", file); fclose(file);
        char path[4096];
        assert(WideCharToMultiByte(CP_UTF8, 0, files[i], -1, path, sizeof(path), NULL, NULL));
        expected_rom = path;
        char *args[] = {"fe8", path, NULL};
        assert(fe8_windows_launch(2, args, game) == 0);
        if (i == 1) {
            expect_resume = 1;
            char *resume[] = {"fe8", "--desktop-rom", path, "--resume", NULL};
            assert(fe8_windows_launch(4, resume, game) == 0);
        }
        assert(DeleteFileW(files[i]));
        assert(DeleteFileW(sibling));
    }
    /* Moving/renaming a ROM must retain its saves, including F5/F8 storage. */
    assert(calls == 3 && !strcmp(save_paths[0], save_paths[1]));
    wchar_t cartridge[4096];
    assert(MultiByteToWideChar(CP_UTF8, 0, save_paths[0], -1, cartridge, 4096));
    assert(DeleteFileW(cartridge));
    char *name = strrchr(save_paths[0], '\\');
    if (!name) name = strrchr(save_paths[0], '/');
    assert(name);
    strcpy(name + 1, "startup.log");
    wchar_t log[4096];
    assert(MultiByteToWideChar(CP_UTF8, 0, save_paths[0], -1, log, 4096));
    assert(GetFileAttributesW(log) != INVALID_FILE_ATTRIBUTES);
    assert(_wfreopen(L"NUL", L"w", stderr));
    assert(DeleteFileW(log));
    *name = '\0';
    assert(MultiByteToWideChar(CP_UTF8, 0, save_paths[0], -1, log, 4096));
    assert(RemoveDirectoryW(log));
    puts("Windows launcher: CLI passthrough, Unicode ROM paths, stable saves and startup log passed");
    return 0;
}
