#include "macos_library.h"

#include <stdio.h>

int fe8_macos_run_library(const char *executable_path) {
    (void)executable_path;
    fprintf(stderr, "The graphical ROM library is currently available on macOS only.\n");
    return 1;
}
