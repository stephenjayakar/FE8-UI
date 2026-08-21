#include "gba_rom_info.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static void usage(const char *program) {
    fprintf(stderr,
        "Usage: %s [--json] [--strict] ROM.gba [ROM.gba ...]\n"
        "Validate GBA headers and print the metadata used by the FE8 library.\n",
        program);
}

static void print_json_string(const char *text) {
    const unsigned char *cursor = (const unsigned char *)(text ? text : "");
    putchar('"');
    while (*cursor) {
        switch (*cursor) {
        case '"': fputs("\\\"", stdout); break;
        case '\\': fputs("\\\\", stdout); break;
        case '\b': fputs("\\b", stdout); break;
        case '\f': fputs("\\f", stdout); break;
        case '\n': fputs("\\n", stdout); break;
        case '\r': fputs("\\r", stdout); break;
        case '\t': fputs("\\t", stdout); break;
        default:
            if (*cursor < 0x20)
                printf("\\u%04x", *cursor);
            else
                putchar(*cursor);
            break;
        }
        ++cursor;
    }
    putchar('"');
}

static void print_indented_lines(const char *text, const char *indent) {
    const char *cursor = text ? text : "";
    const char *line = cursor;
    while (*cursor) {
        if (*cursor == '\n') {
            printf("%s%.*s\n", indent, (int)(cursor - line), line);
            line = cursor + 1;
        }
        ++cursor;
    }
    if (cursor != line)
        printf("%s%s\n", indent, line);
}

static const char *read_result_name(Fe8GbaRomReadResult result) {
    switch (result) {
    case FE8_GBA_ROM_READ_OK: return "ok";
    case FE8_GBA_ROM_READ_INVALID_HEADER: return "invalid_header";
    case FE8_GBA_ROM_READ_INVALID_ARGUMENT: return "invalid_argument";
    case FE8_GBA_ROM_READ_OPEN_FAILED: return "open_failed";
    case FE8_GBA_ROM_READ_TOO_SMALL: return "too_small";
    case FE8_GBA_ROM_READ_IO_FAILED: return "io_failed";
    }
    return "unknown";
}

static void print_human(
    const char *path, Fe8GbaRomReadResult result,
    const Fe8GbaRomInfo *info, const char *message) {
    char issues[1024];
    printf("%s\n", path);
    if (result < 0) {
        printf("  Validation: error\n  Reason: %s\n\n",
            message && *message ? message : read_result_name(result));
        return;
    }
    fe8_gba_rom_info_format_issues(info, issues, sizeof(issues));
    printf("  SHA-1: %s\n", info->sha1);
    printf("  Size: %" PRIu64 " bytes (%.2f MiB)\n", info->file_size,
        (double)info->file_size / (1024.0 * 1024.0));
    printf("  Header: %s · %s · maker %s · revision %u\n",
        info->internal_title[0] ? info->internal_title : "(untitled)",
        info->game_code[0] ? info->game_code : "(no code)",
        info->maker_code[0] ? info->maker_code : "--", info->version);
    printf("  Compatibility: %s\n",
        fe8_gba_rom_info_compatibility_label(info));
    printf("  Header checksum: stored 0x%02X, calculated 0x%02X\n",
        info->stored_header_checksum, info->computed_header_checksum);
    if (result == FE8_GBA_ROM_READ_INVALID_HEADER)
        printf("  Validation: invalid GBA header\n");
    else if (fe8_gba_rom_info_has_warnings(info))
        printf("  Validation: valid with warnings\n");
    else
        printf("  Validation: valid\n");
    if (issues[0]) {
        puts("  Notes:");
        print_indented_lines(issues, "    ");
    }
    putchar('\n');
}

static void print_json_entry(
    const char *path, Fe8GbaRomReadResult result,
    const Fe8GbaRomInfo *info, const char *message) {
    char issues[1024];
    fputs("  {\"path\":", stdout);
    print_json_string(path);
    fputs(",\"result\":", stdout);
    print_json_string(read_result_name(result));
    if (result < 0) {
        fputs(",\"error\":", stdout);
        print_json_string(message);
        putchar('}');
        return;
    }
    fe8_gba_rom_info_format_issues(info, issues, sizeof(issues));
    fputs(",\"sha1\":", stdout);
    print_json_string(info->sha1);
    printf(",\"size_bytes\":%" PRIu64, info->file_size);
    fputs(",\"internal_title\":", stdout);
    print_json_string(info->internal_title);
    fputs(",\"game_code\":", stdout);
    print_json_string(info->game_code);
    fputs(",\"maker_code\":", stdout);
    print_json_string(info->maker_code);
    printf(",\"revision\":%u", info->version);
    fputs(",\"compatibility\":", stdout);
    print_json_string(fe8_gba_rom_info_compatibility_label(info));
    printf(",\"fe8u_family\":%s", fe8_gba_rom_info_is_fe8u_family(info) ?
        "true" : "false");
    printf(",\"importable\":%s", fe8_gba_rom_info_is_importable(info) ?
        "true" : "false");
    printf(",\"header_checksum\":{\"stored\":%u,\"calculated\":%u,\"valid\":%s}",
        info->stored_header_checksum, info->computed_header_checksum,
        (info->issues & FE8_GBA_ROM_ISSUE_HEADER_CHECKSUM) ? "false" : "true");
    printf(",\"issue_mask\":%u,\"issues\":", info->issues);
    print_json_string(issues);
    putchar('}');
}

int main(int argc, char **argv) {
    int json = 0;
    int strict = 0;
    int first_path = 0;
    int exit_status = 0;
    int i;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--json") == 0) {
            json = 1;
        } else if (strcmp(argv[i], "--strict") == 0) {
            strict = 1;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            return 0;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            usage(argv[0]);
            return 2;
        } else {
            first_path = i;
            break;
        }
    }
    if (first_path == 0 || first_path >= argc) {
        usage(argv[0]);
        return 2;
    }

    if (json)
        puts("[");
    for (i = first_path; i < argc; ++i) {
        Fe8GbaRomInfo info;
        char error[1024];
        Fe8GbaRomReadResult result = fe8_gba_rom_info_read_file(
            argv[i], &info, error, sizeof(error));
        if (result != FE8_GBA_ROM_READ_OK ||
                (strict && fe8_gba_rom_info_has_warnings(&info)))
            exit_status = 1;
        if (json) {
            if (i != first_path)
                puts(",");
            print_json_entry(argv[i], result, &info, error);
        } else {
            print_human(argv[i], result, &info, error);
        }
    }
    if (json)
        puts("\n]");
    return exit_status;
}
