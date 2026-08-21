#ifndef FE8_GBA_ROM_INFO_H
#define FE8_GBA_ROM_INFO_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    FE8_GBA_HEADER_SIZE = 0xC0,
    FE8_GBA_SHA1_HEX_LENGTH = 40,
};

typedef enum Fe8GbaRomIssue {
    FE8_GBA_ROM_ISSUE_NONE = 0,
    FE8_GBA_ROM_ISSUE_NINTENDO_LOGO = 1u << 0,
    FE8_GBA_ROM_ISSUE_FIXED_VALUE = 1u << 1,
    FE8_GBA_ROM_ISSUE_HEADER_CHECKSUM = 1u << 2,
    FE8_GBA_ROM_ISSUE_INTERNAL_TITLE = 1u << 3,
    FE8_GBA_ROM_ISSUE_GAME_CODE = 1u << 4,
    FE8_GBA_ROM_ISSUE_NONSTANDARD_SIZE = 1u << 5,
} Fe8GbaRomIssue;

typedef enum Fe8GbaRomReadResult {
    FE8_GBA_ROM_READ_OK = 0,
    FE8_GBA_ROM_READ_INVALID_HEADER = 1,
    FE8_GBA_ROM_READ_INVALID_ARGUMENT = -1,
    FE8_GBA_ROM_READ_OPEN_FAILED = -2,
    FE8_GBA_ROM_READ_TOO_SMALL = -3,
    FE8_GBA_ROM_READ_IO_FAILED = -4,
} Fe8GbaRomReadResult;

typedef struct Fe8GbaRomInfo {
    uint64_t file_size;
    char sha1[FE8_GBA_SHA1_HEX_LENGTH + 1];
    char internal_title[13];
    char game_code[5];
    char maker_code[3];
    uint8_t version;
    uint8_t stored_header_checksum;
    uint8_t computed_header_checksum;
    uint32_t issues;
} Fe8GbaRomInfo;

/* Parse and validate the first 0xC0 bytes of a GBA ROM image. */
Fe8GbaRomReadResult fe8_gba_rom_info_parse_header(
    const uint8_t *header, size_t header_size, uint64_t file_size,
    Fe8GbaRomInfo *info, char *error, size_t error_size);

/* Stream a ROM from disk, calculate its SHA-1, and validate its header. */
Fe8GbaRomReadResult fe8_gba_rom_info_read_file(
    const char *path, Fe8GbaRomInfo *info, char *error, size_t error_size);

uint8_t fe8_gba_rom_header_checksum(const uint8_t *header, size_t header_size);
int fe8_gba_rom_info_is_importable(const Fe8GbaRomInfo *info);
int fe8_gba_rom_info_has_warnings(const Fe8GbaRomInfo *info);
int fe8_gba_rom_info_is_fe8u_family(const Fe8GbaRomInfo *info);
const char *fe8_gba_rom_info_compatibility_label(const Fe8GbaRomInfo *info);
void fe8_gba_rom_info_format_issues(
    const Fe8GbaRomInfo *info, char *buffer, size_t buffer_size);

#ifdef __cplusplus
}
#endif

#endif
