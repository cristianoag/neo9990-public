/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NEO_FDT_H
#define NEO_FDT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum neo_boot_result {
    NEO_BOOT_OK,
    NEO_BOOT_BAD_FDT,
    NEO_BOOT_WRONG_BOARD,
    NEO_BOOT_NO_FRAMEBUFFER,
    NEO_BOOT_BAD_FRAMEBUFFER,
    NEO_BOOT_BAD_MODE
};

struct neo_framebuffer {
    uint64_t address, size;
    uint32_t width, height, stride;
    bool swap_red_blue;
};

struct neo_boot {
    struct neo_framebuffer fb;
    bool cartridge;
};

enum neo_boot_result neo_fdt_parse(const void *data, size_t available,
                                   struct neo_boot *boot);
bool neo_fb_overlaps(const struct neo_framebuffer *fb, uint64_t start, uint64_t size);

#endif
