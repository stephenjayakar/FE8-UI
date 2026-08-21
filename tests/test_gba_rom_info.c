#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "gba_rom_info.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <unistd.h>
#endif

static const uint8_t kNintendoLogo[156] = {
    0x24, 0xFF, 0xAE, 0x51, 0x69, 0x9A, 0xA2, 0x21,
    0x3D, 0x84, 0x82, 0x0A, 0x84, 0xE4, 0x09, 0xAD,
    0x11, 0x24, 0x8B, 0x98, 0xC0, 0x81, 0x7F, 0x21,
    0xA3, 0x52, 0xBE, 0x19, 0x93, 0x09, 0xCE, 0x20,
    0x10, 0x46, 0x4A, 0x4A, 0xF8, 0x27, 0x31, 0xEC,
    0x58, 0xC7, 0xE8, 0x33, 0x82, 0xE3, 0xCE, 0xBF,
    0x85, 0xF4, 0xDF, 0x94, 0xCE, 0x4B, 0x09, 0xC1,
    0x94, 0x56, 0x8A, 0xC0, 0x13, 0x72, 0xA7, 0xFC,
    0x9F, 0x84, 0x4D, 0x73, 0xA3, 0xCA, 0x9A, 0x61,
    0x58, 0x97, 0xA3, 0x27, 0xFC, 0x03, 0x98, 0x76,
    0x23, 0x1D, 0xC7, 0x61, 0x03, 0x04, 0xAE, 0x56,
    0xBF, 0x38, 0x84, 0x00, 0x40, 0xA7, 0x0E, 0xFD,
    0xFF, 0x52, 0xFE, 0x03, 0x6F, 0x95, 0x30, 0xF1,
    0x97, 0xFB, 0xC0, 0x85, 0x60, 0xD6, 0x80, 0x25,
    0xA9, 0x63, 0xBE, 0x03, 0x01, 0x4E, 0x38, 0xE2,
    0xF9, 0xA2, 0x34, 0xFF, 0xBB, 0x3E, 0x03, 0x44,
    0x78, 0x00, 0x90, 0xCB, 0x88, 0x11, 0x3A, 0x94,
    0x65, 0xC0, 0x7C, 0x63, 0x87, 0xF0, 0x3C, 0xAF,
    0xD6, 0x25, 0xE4, 0x8B, 0x38, 0x0A, 0xAC, 0x72,
    0x21, 0xD4, 0xF8, 0x07,
};

static void make_valid_rom(uint8_t *rom, size_t size, const char *game_code) {
    size_t i;
    memset(rom, 0, size);
    for (i = FE8_GBA_HEADER_SIZE; i < size; ++i)
        rom[i] = (uint8_t)(i * 37u + 11u);
    memcpy(rom + 0x04, kNintendoLogo, sizeof(kNintendoLogo));
    memcpy(rom + 0xA0, "TEST ROM    ", 12);
    memcpy(rom + 0xAC, game_code, 4);
    memcpy(rom + 0xB0, "01", 2);
    rom[0xB2] = 0x96;
    rom[0xBC] = 3;
    rom[0xBD] = fe8_gba_rom_header_checksum(rom, size);
}

static int write_temp_file(
    const uint8_t *bytes, size_t size, char *path, size_t path_size) {
    FILE *file;
    int success;
#ifdef _WIN32
    char temporary[L_tmpnam_s];
    if (tmpnam_s(temporary, sizeof(temporary)) != 0)
        return 0;
    snprintf(path, path_size, "%s", temporary);
    file = fopen(path, "wb");
#else
    char temporary[] = "/tmp/fe8-rom-info-XXXXXX";
    int descriptor = mkstemp(temporary);
    if (descriptor < 0)
        return 0;
    snprintf(path, path_size, "%s", temporary);
    file = fdopen(descriptor, "wb");
    if (!file) {
        close(descriptor);
        remove(path);
        return 0;
    }
#endif
    if (!file)
        return 0;
    success = fwrite(bytes, 1, size, file) == size;
    if (fclose(file) != 0)
        success = 0;
    if (!success)
        remove(path);
    return success;
}

static void test_valid_header(void) {
    uint8_t rom[512];
    Fe8GbaRomInfo info;
    char error[512];
    make_valid_rom(rom, sizeof(rom), "BE8E");
    assert(fe8_gba_rom_info_parse_header(rom, sizeof(rom), sizeof(rom),
        &info, error, sizeof(error)) == FE8_GBA_ROM_READ_OK);
    assert(info.issues == FE8_GBA_ROM_ISSUE_NONE);
    assert(strcmp(info.internal_title, "TEST ROM") == 0);
    assert(strcmp(info.game_code, "BE8E") == 0);
    assert(strcmp(info.maker_code, "01") == 0);
    assert(info.version == 3);
    assert(info.stored_header_checksum == info.computed_header_checksum);
    assert(fe8_gba_rom_info_is_importable(&info));
    assert(fe8_gba_rom_info_is_fe8u_family(&info));
    assert(strcmp(fe8_gba_rom_info_compatibility_label(&info),
        "FE8U-compatible") == 0);
}

static void test_nonfatal_checksum_warning(void) {
    uint8_t rom[512];
    Fe8GbaRomInfo info;
    char error[512];
    char issues[512];
    make_valid_rom(rom, sizeof(rom), "BE8E");
    rom[0xA0] ^= 1;
    assert(fe8_gba_rom_info_parse_header(rom, sizeof(rom), sizeof(rom),
        &info, error, sizeof(error)) == FE8_GBA_ROM_READ_OK);
    assert(info.issues & FE8_GBA_ROM_ISSUE_HEADER_CHECKSUM);
    assert(fe8_gba_rom_info_is_importable(&info));
    assert(fe8_gba_rom_info_has_warnings(&info));
    fe8_gba_rom_info_format_issues(&info, issues, sizeof(issues));
    assert(strstr(issues, "checksum") != NULL);
}

static void test_fatal_logo_error(void) {
    uint8_t rom[512];
    Fe8GbaRomInfo info;
    char error[512];
    make_valid_rom(rom, sizeof(rom), "BE8E");
    rom[0x04] ^= 1;
    assert(fe8_gba_rom_info_parse_header(rom, sizeof(rom), sizeof(rom),
        &info, error, sizeof(error)) == FE8_GBA_ROM_READ_INVALID_HEADER);
    assert(info.issues & FE8_GBA_ROM_ISSUE_NINTENDO_LOGO);
    assert(!fe8_gba_rom_info_is_importable(&info));
    assert(strstr(error, "logo") != NULL);
}

static void test_fatal_fixed_value_error(void) {
    uint8_t rom[512];
    Fe8GbaRomInfo info;
    char error[512];
    make_valid_rom(rom, sizeof(rom), "BE8E");
    rom[0xB2] = 0;
    assert(fe8_gba_rom_info_parse_header(rom, sizeof(rom), sizeof(rom),
        &info, error, sizeof(error)) == FE8_GBA_ROM_READ_INVALID_HEADER);
    assert(info.issues & FE8_GBA_ROM_ISSUE_FIXED_VALUE);
    assert(!fe8_gba_rom_info_is_importable(&info));
    assert(strstr(error, "0xB2") != NULL);
}

static void test_metadata_and_size_warnings(void) {
    uint8_t rom[512];
    Fe8GbaRomInfo info;
    char error[512];
    char issues[512];
    make_valid_rom(rom, sizeof(rom), "BE8E");
    rom[0xA0] = 1;
    memset(rom + 0xAC, ' ', 4);
    rom[0xBD] = fe8_gba_rom_header_checksum(rom, sizeof(rom));
    assert(fe8_gba_rom_info_parse_header(rom, sizeof(rom),
        UINT64_C(32) * 1024 * 1024 + 1, &info, error, sizeof(error)) ==
        FE8_GBA_ROM_READ_OK);
    assert(info.issues & FE8_GBA_ROM_ISSUE_INTERNAL_TITLE);
    assert(info.issues & FE8_GBA_ROM_ISSUE_GAME_CODE);
    assert(info.issues & FE8_GBA_ROM_ISSUE_NONSTANDARD_SIZE);
    assert(!(info.issues & FE8_GBA_ROM_ISSUE_HEADER_CHECKSUM));
    assert(fe8_gba_rom_info_is_importable(&info));
    fe8_gba_rom_info_format_issues(&info, issues, sizeof(issues));
    assert(strstr(issues, "internal title") != NULL);
    assert(strstr(issues, "game code") != NULL);
    assert(strstr(issues, "32 MiB") != NULL);
}

static void test_importable_requires_parsed_file(void) {
    Fe8GbaRomInfo info;
    memset(&info, 0, sizeof(info));
    assert(!fe8_gba_rom_info_is_importable(&info));
    assert(!fe8_gba_rom_info_has_warnings(&info));
}

static void test_invalid_arguments(void) {
    uint8_t rom[FE8_GBA_HEADER_SIZE] = {0};
    Fe8GbaRomInfo info;
    char error[512];
    assert(fe8_gba_rom_info_parse_header(NULL, sizeof(rom), sizeof(rom),
        &info, error, sizeof(error)) == FE8_GBA_ROM_READ_INVALID_ARGUMENT);
    assert(strstr(error, "invalid argument") != NULL);
    assert(fe8_gba_rom_info_read_file(NULL, &info, error, sizeof(error)) ==
        FE8_GBA_ROM_READ_INVALID_ARGUMENT);
    assert(!fe8_gba_rom_info_is_importable(&info));
}

static void test_open_failure(void) {
    Fe8GbaRomInfo info;
    char error[512];
#ifdef _WIN32
    const char *path = "Z:\\fe8-rom-info\\definitely-missing.gba";
#else
    const char *path = "/tmp/fe8-rom-info/definitely-missing/file.gba";
#endif
    assert(fe8_gba_rom_info_read_file(path, &info, error, sizeof(error)) ==
        FE8_GBA_ROM_READ_OPEN_FAILED);
    assert(strstr(error, "Unable to open") != NULL);
    assert(!fe8_gba_rom_info_is_importable(&info));
}

static void test_truncated_header(void) {
    uint8_t rom[FE8_GBA_HEADER_SIZE - 1] = {0};
    Fe8GbaRomInfo info;
    char error[512];
    assert(fe8_gba_rom_info_parse_header(rom, sizeof(rom), sizeof(rom),
        &info, error, sizeof(error)) == FE8_GBA_ROM_READ_TOO_SMALL);
    assert(strstr(error, "too small") != NULL);
}

static void test_streaming_sha1(void) {
    uint8_t rom[512];
    Fe8GbaRomInfo info;
    char error[512];
    char path[512];
    make_valid_rom(rom, sizeof(rom), "ABCD");
    assert(write_temp_file(rom, sizeof(rom), path, sizeof(path)));
    assert(fe8_gba_rom_info_read_file(path, &info, error, sizeof(error)) ==
        FE8_GBA_ROM_READ_OK);
    remove(path);
    assert(strcmp(info.sha1, "6df6d1f18b530f3e46d72ec4b4363dabd08a4439") == 0);
    assert(strcmp(info.game_code, "ABCD") == 0);
    assert(!fe8_gba_rom_info_is_fe8u_family(&info));
    assert(strcmp(fe8_gba_rom_info_compatibility_label(&info),
        "GBA; standard rendering") == 0);
}

int main(void) {
    test_valid_header();
    test_nonfatal_checksum_warning();
    test_fatal_logo_error();
    test_fatal_fixed_value_error();
    test_metadata_and_size_warnings();
    test_importable_requires_parsed_file();
    test_invalid_arguments();
    test_open_failure();
    test_truncated_header();
    test_streaming_sha1();
    puts("gba_rom_info tests passed");
    return 0;
}
