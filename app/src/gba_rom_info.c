#include "gba_rom_info.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#define FE8_GBA_MAX_STANDARD_ROM_SIZE (32u * 1024u * 1024u)

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

typedef struct Fe8Sha1Context {
    uint32_t state[5];
    uint64_t bit_count;
    uint8_t buffer[64];
    size_t buffer_size;
} Fe8Sha1Context;

static uint32_t rotate_left(uint32_t value, unsigned count) {
    return (value << count) | (value >> (32u - count));
}

static uint32_t read_be32(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
        ((uint32_t)bytes[2] << 8) | bytes[3];
}

static void sha1_transform(Fe8Sha1Context *context, const uint8_t block[64]) {
    uint32_t words[80];
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t e;
    unsigned i;

    for (i = 0; i < 16; ++i)
        words[i] = read_be32(block + i * 4);
    for (; i < 80; ++i)
        words[i] = rotate_left(words[i - 3] ^ words[i - 8] ^
            words[i - 14] ^ words[i - 16], 1);

    a = context->state[0];
    b = context->state[1];
    c = context->state[2];
    d = context->state[3];
    e = context->state[4];

    for (i = 0; i < 80; ++i) {
        uint32_t function;
        uint32_t constant;
        uint32_t temporary;
        if (i < 20) {
            function = (b & c) | ((~b) & d);
            constant = UINT32_C(0x5A827999);
        } else if (i < 40) {
            function = b ^ c ^ d;
            constant = UINT32_C(0x6ED9EBA1);
        } else if (i < 60) {
            function = (b & c) | (b & d) | (c & d);
            constant = UINT32_C(0x8F1BBCDC);
        } else {
            function = b ^ c ^ d;
            constant = UINT32_C(0xCA62C1D6);
        }
        temporary = rotate_left(a, 5) + function + e + constant + words[i];
        e = d;
        d = c;
        c = rotate_left(b, 30);
        b = a;
        a = temporary;
    }

    context->state[0] += a;
    context->state[1] += b;
    context->state[2] += c;
    context->state[3] += d;
    context->state[4] += e;
}

static void sha1_init(Fe8Sha1Context *context) {
    context->state[0] = UINT32_C(0x67452301);
    context->state[1] = UINT32_C(0xEFCDAB89);
    context->state[2] = UINT32_C(0x98BADCFE);
    context->state[3] = UINT32_C(0x10325476);
    context->state[4] = UINT32_C(0xC3D2E1F0);
    context->bit_count = 0;
    context->buffer_size = 0;
}

static void sha1_update(Fe8Sha1Context *context, const uint8_t *data, size_t size) {
    context->bit_count += (uint64_t)size * 8;
    while (size > 0) {
        size_t available = sizeof(context->buffer) - context->buffer_size;
        size_t copied = size < available ? size : available;
        memcpy(context->buffer + context->buffer_size, data, copied);
        context->buffer_size += copied;
        data += copied;
        size -= copied;
        if (context->buffer_size == sizeof(context->buffer)) {
            sha1_transform(context, context->buffer);
            context->buffer_size = 0;
        }
    }
}

static void sha1_final(Fe8Sha1Context *context, uint8_t digest[20]) {
    uint8_t padding[64] = {0x80};
    uint8_t length[8];
    uint64_t original_bit_count = context->bit_count;
    size_t padding_size = context->buffer_size < 56 ?
        56 - context->buffer_size : 120 - context->buffer_size;
    unsigned i;

    sha1_update(context, padding, padding_size);
    for (i = 0; i < 8; ++i)
        length[7 - i] = (uint8_t)(original_bit_count >> (i * 8));
    sha1_update(context, length, sizeof(length));

    for (i = 0; i < 5; ++i) {
        digest[i * 4] = (uint8_t)(context->state[i] >> 24);
        digest[i * 4 + 1] = (uint8_t)(context->state[i] >> 16);
        digest[i * 4 + 2] = (uint8_t)(context->state[i] >> 8);
        digest[i * 4 + 3] = (uint8_t)context->state[i];
    }
}

static void set_error(char *error, size_t error_size, const char *message) {
    if (!error || !error_size)
        return;
    snprintf(error, error_size, "%s", message ? message : "Unknown error");
}

static void set_system_error(
    char *error, size_t error_size, const char *prefix, const char *path,
    int error_number) {
    if (!error || !error_size)
        return;
    if (!error_number)
        error_number = EIO;
    snprintf(error, error_size, "%s '%s': %s", prefix,
        path ? path : "", strerror(error_number));
}

static int header_field_is_printable(
    const uint8_t *bytes, size_t size, int allow_empty) {
    size_t end = size;
    size_t i;
    while (end > 0 && (bytes[end - 1] == 0 || bytes[end - 1] == 0xFF ||
            bytes[end - 1] == ' '))
        --end;
    if (!end)
        return allow_empty;
    for (i = 0; i < end; ++i) {
        if (bytes[i] < 0x20 || bytes[i] > 0x7E)
            return 0;
    }
    return 1;
}

static void copy_header_field(
    char *destination, size_t destination_size,
    const uint8_t *source, size_t source_size) {
    size_t end = source_size;
    size_t i;
    while (end > 0 && (source[end - 1] == 0 || source[end - 1] == 0xFF ||
            source[end - 1] == ' '))
        --end;
    if (end >= destination_size)
        end = destination_size - 1;
    for (i = 0; i < end; ++i) {
        uint8_t value = source[i];
        destination[i] = value >= 0x20 && value <= 0x7E ? (char)value : '?';
    }
    destination[end] = '\0';
}

static void append_issue(char *buffer, size_t buffer_size, const char *issue) {
    size_t used;
    if (!buffer || !buffer_size || !issue)
        return;
    used = strlen(buffer);
    if (used && used + 2 < buffer_size) {
        buffer[used++] = '\n';
        buffer[used] = '\0';
    }
    if (used < buffer_size - 1)
        snprintf(buffer + used, buffer_size - used, "%s", issue);
}

uint8_t fe8_gba_rom_header_checksum(const uint8_t *header, size_t header_size) {
    unsigned sum = 0;
    size_t i;
    if (!header || header_size < 0xBD)
        return 0;
    for (i = 0xA0; i < 0xBD; ++i)
        sum += header[i];
    return (uint8_t)(-(int)(0x19 + sum));
}

Fe8GbaRomReadResult fe8_gba_rom_info_parse_header(
    const uint8_t *header, size_t header_size, uint64_t file_size,
    Fe8GbaRomInfo *info, char *error, size_t error_size) {
    uint32_t fatal_issues;
    if (info)
        memset(info, 0, sizeof(*info));
    if (!header || !info) {
        set_error(error, error_size, "ROM header parser received an invalid argument.");
        return FE8_GBA_ROM_READ_INVALID_ARGUMENT;
    }
    if (header_size < FE8_GBA_HEADER_SIZE) {
        set_error(error, error_size,
            "The file is too small to contain a complete Game Boy Advance header.");
        return FE8_GBA_ROM_READ_TOO_SMALL;
    }

    info->file_size = file_size;
    copy_header_field(info->internal_title, sizeof(info->internal_title),
        header + 0xA0, 12);
    copy_header_field(info->game_code, sizeof(info->game_code), header + 0xAC, 4);
    copy_header_field(info->maker_code, sizeof(info->maker_code), header + 0xB0, 2);
    info->version = header[0xBC];
    info->stored_header_checksum = header[0xBD];
    info->computed_header_checksum = fe8_gba_rom_header_checksum(header, header_size);

    if (memcmp(header + 0x04, kNintendoLogo, sizeof(kNintendoLogo)) != 0)
        info->issues |= FE8_GBA_ROM_ISSUE_NINTENDO_LOGO;
    if (header[0xB2] != 0x96)
        info->issues |= FE8_GBA_ROM_ISSUE_FIXED_VALUE;
    if (info->stored_header_checksum != info->computed_header_checksum)
        info->issues |= FE8_GBA_ROM_ISSUE_HEADER_CHECKSUM;
    if (!header_field_is_printable(header + 0xA0, 12, 0))
        info->issues |= FE8_GBA_ROM_ISSUE_INTERNAL_TITLE;
    if (!header_field_is_printable(header + 0xAC, 4, 0))
        info->issues |= FE8_GBA_ROM_ISSUE_GAME_CODE;
    if (file_size > FE8_GBA_MAX_STANDARD_ROM_SIZE)
        info->issues |= FE8_GBA_ROM_ISSUE_NONSTANDARD_SIZE;

    fatal_issues = FE8_GBA_ROM_ISSUE_NINTENDO_LOGO |
        FE8_GBA_ROM_ISSUE_FIXED_VALUE;
    if (info->issues & fatal_issues) {
        fe8_gba_rom_info_format_issues(info, error, error_size);
        return FE8_GBA_ROM_READ_INVALID_HEADER;
    }
    if (error && error_size)
        error[0] = '\0';
    return FE8_GBA_ROM_READ_OK;
}

Fe8GbaRomReadResult fe8_gba_rom_info_read_file(
    const char *path, Fe8GbaRomInfo *info, char *error, size_t error_size) {
    FILE *file;
    uint8_t block[64 * 1024];
    uint8_t header[FE8_GBA_HEADER_SIZE];
    size_t header_size = 0;
    uint64_t file_size = 0;
    Fe8Sha1Context sha1;
    uint8_t digest[20];
    Fe8GbaRomReadResult result;
    size_t count;
    unsigned i;

    if (info)
        memset(info, 0, sizeof(*info));
    if (!path || !*path || !info) {
        set_error(error, error_size, "ROM reader received an invalid argument.");
        return FE8_GBA_ROM_READ_INVALID_ARGUMENT;
    }
    file = fopen(path, "rb");
    if (!file) {
        int open_error = errno;
        set_system_error(error, error_size, "Unable to open", path, open_error);
        return FE8_GBA_ROM_READ_OPEN_FAILED;
    }

    sha1_init(&sha1);
    while ((count = fread(block, 1, sizeof(block), file)) > 0) {
        size_t header_remaining = sizeof(header) - header_size;
        size_t header_count = count < header_remaining ? count : header_remaining;
        if (header_count) {
            memcpy(header + header_size, block, header_count);
            header_size += header_count;
        }
        sha1_update(&sha1, block, count);
        file_size += count;
    }
    if (ferror(file)) {
        int read_error = errno;
        fclose(file);
        set_system_error(error, error_size, "Unable to read", path, read_error);
        return FE8_GBA_ROM_READ_IO_FAILED;
    }
    if (fclose(file) != 0) {
        int close_error = errno;
        set_system_error(
            error, error_size, "Unable to finish reading", path, close_error);
        return FE8_GBA_ROM_READ_IO_FAILED;
    }

    result = fe8_gba_rom_info_parse_header(
        header, header_size, file_size, info, error, error_size);
    if (result < 0)
        return result;

    sha1_final(&sha1, digest);
    for (i = 0; i < sizeof(digest); ++i)
        snprintf(info->sha1 + i * 2, 3, "%02x", digest[i]);
    info->sha1[FE8_GBA_SHA1_HEX_LENGTH] = '\0';
    return result;
}

int fe8_gba_rom_info_is_importable(const Fe8GbaRomInfo *info) {
    uint32_t fatal_issues = FE8_GBA_ROM_ISSUE_NINTENDO_LOGO |
        FE8_GBA_ROM_ISSUE_FIXED_VALUE;
    return info && info->file_size >= FE8_GBA_HEADER_SIZE &&
        !(info->issues & fatal_issues);
}

int fe8_gba_rom_info_has_warnings(const Fe8GbaRomInfo *info) {
    uint32_t warnings = FE8_GBA_ROM_ISSUE_HEADER_CHECKSUM |
        FE8_GBA_ROM_ISSUE_INTERNAL_TITLE | FE8_GBA_ROM_ISSUE_GAME_CODE |
        FE8_GBA_ROM_ISSUE_NONSTANDARD_SIZE;
    return info && (info->issues & warnings) != 0;
}

int fe8_gba_rom_info_is_fe8u_family(const Fe8GbaRomInfo *info) {
    return info && strcmp(info->game_code, "BE8E") == 0;
}

const char *fe8_gba_rom_info_compatibility_label(const Fe8GbaRomInfo *info) {
    if (!info)
        return "Unknown";
    if (strcmp(info->game_code, "BE8E") == 0)
        return "FE8U-compatible";
    if (strncmp(info->game_code, "BE8", 3) == 0)
        return "FE8-family; extensions may vary";
    return "GBA; standard rendering";
}

void fe8_gba_rom_info_format_issues(
    const Fe8GbaRomInfo *info, char *buffer, size_t buffer_size) {
    char checksum[128];
    if (!buffer || !buffer_size)
        return;
    buffer[0] = '\0';
    if (!info) {
        append_issue(buffer, buffer_size, "ROM metadata is unavailable.");
        return;
    }
    if (info->issues & FE8_GBA_ROM_ISSUE_NINTENDO_LOGO)
        append_issue(buffer, buffer_size,
            "The Nintendo logo data in the GBA header is invalid.");
    if (info->issues & FE8_GBA_ROM_ISSUE_FIXED_VALUE)
        append_issue(buffer, buffer_size,
            "The required GBA header byte at 0xB2 is not 0x96.");
    if (info->issues & FE8_GBA_ROM_ISSUE_HEADER_CHECKSUM) {
        snprintf(checksum, sizeof(checksum),
            "The GBA header checksum does not match (stored 0x%02X, calculated 0x%02X).",
            info->stored_header_checksum, info->computed_header_checksum);
        append_issue(buffer, buffer_size, checksum);
    }
    if (info->issues & FE8_GBA_ROM_ISSUE_INTERNAL_TITLE)
        append_issue(buffer, buffer_size,
            "The internal title is empty or contains non-printable characters.");
    if (info->issues & FE8_GBA_ROM_ISSUE_GAME_CODE)
        append_issue(buffer, buffer_size,
            "The game code is empty or contains non-printable characters.");
    if (info->issues & FE8_GBA_ROM_ISSUE_NONSTANDARD_SIZE)
        append_issue(buffer, buffer_size,
            "The ROM is larger than the standard 32 MiB GBA address space.");
}
