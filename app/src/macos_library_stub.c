#include "macos_library.h"

#include <stdio.h>

int fe8_macos_run_library(const char *executable_path, int voxel_enabled) {
    (void)executable_path;
    (void)voxel_enabled;
    fprintf(stderr, "The graphical ROM library is currently available on macOS only.\n");
    return 1;
}
