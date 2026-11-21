/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NEO_V9990_H
#define NEO_V9990_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NEO_VRAM_SIZE (512u * 1024u)
#define NEO_UC_HZ UINT64_C(42954540)
#define NEO_LINE_TICKS 2736u
#define NEO_WIDTH 256u
#define NEO_HEIGHT 212u

enum neo_vdp_result {
    NEO_VDP_OK,
    NEO_VDP_BAD_ARGUMENT,
    NEO_VDP_TIME_REVERSED,
    NEO_VDP_UNSUPPORTED_COMMAND,
    NEO_VDP_UNSUPPORTED_DISPLAY,
    NEO_VDP_UNSUPPORTED_IRQ
};

struct neo_v9990 {
    uint8_t vram[NEO_VRAM_SIZE];
    uint8_t regs[64], palette[256];
    uint8_t select, read_buffer, pending, status, fault_register, fault_value;
    bool system_reset, pal, display_enabled, vblank_seen;
    uint64_t frame_start, now;
    enum neo_vdp_result fault;
};

void neo_vdp_power_on(struct neo_v9990 *vdp, uint64_t ticks);
void neo_vdp_reset(struct neo_v9990 *vdp, uint64_t ticks);
enum neo_vdp_result neo_vdp_tick(struct neo_v9990 *vdp, uint64_t ticks);
enum neo_vdp_result neo_vdp_read(struct neo_v9990 *vdp, uint16_t port,
                               uint64_t ticks, uint8_t *value);
enum neo_vdp_result neo_vdp_write(struct neo_v9990 *vdp, uint16_t port,
                                uint8_t value, uint64_t ticks);
bool neo_vdp_irq(const struct neo_v9990 *vdp);
enum neo_vdp_result neo_vdp_render_line(struct neo_v9990 *vdp, unsigned y,
                                      uint32_t *rgb, size_t count);
enum neo_vdp_result neo_vdp_demo(struct neo_v9990 *vdp);

#endif
