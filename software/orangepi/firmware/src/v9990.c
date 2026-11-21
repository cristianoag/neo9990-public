/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Adapted in 2026 for neo9990 from openMSX V9990.cc, V9990VRAM.{cc,hh},
 * V9990.hh, V9990DisplayTiming.hh and V9990BitmapConverter.cc.
 * Copyright: the respective openMSX authors; see third_party/openmsx.
 * This deliberately limited C port replaces the scheduler and SDL renderer.
 */
#include "neo_v9990.h"

static const uint8_t access[64] = {
    2,2,2,2,2,2,3,3,3,3,3,3,3,2,2,3,
    3,3,3,3,3,3,3,3,3,3,3,3,2,0,0,0,
    2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    2,2,2,2,2,1,1,0,0,0,0,0,0,0,0,0
};
static const uint8_t write_mask[32] = {
    255,255,255,255,255,255,255,255,
    255,0x87,255,0x83,0x0f,255,255,255,
    255,255,0xdf,7,255,255,0xc1,7,
    0x3f,0xcf,255,255,255,255,255,255
};

static enum neo_vdp_result fail(struct neo_v9990 *v, enum neo_vdp_result error,
                                uint8_t reg, uint8_t value)
{
    v->fault = error;
    v->fault_register = reg;
    v->fault_value = value;
    return error;
}

static uint32_t address(const struct neo_v9990 *v, unsigned base)
{
    return v->regs[base] | ((uint32_t)v->regs[base+1] << 8) |
           ((uint32_t)(v->regs[base+2] & 7) << 16);
}

static void set_address(struct neo_v9990 *v, unsigned base, uint32_t addr)
{
    v->regs[base] = (uint8_t)addr;
    v->regs[base+1] = (uint8_t)(addr >> 8);
    v->regs[base+2] = (uint8_t)((v->regs[base+2] & 0x80) | ((addr >> 16) & 7));
}

static uint32_t bx(uint32_t addr)
{
    return ((addr & 1u) << 18) | ((addr & 0x7fffeu) >> 1);
}

static uint32_t map_address(const struct neo_v9990 *v, uint32_t addr)
{
    addr &= 0x7ffffu;
    switch (v->regs[6] & 0xc0) {
    case 0: return addr;
    case 0x40:
        return addr < 0x78000u ? bx(addr) :
               addr < 0x7c000u ? addr - 0x3c000u : addr;
    default: return bx(addr);
    }
}

static unsigned top_border(const struct neo_v9990 *v)
{
    int adjust = ((v->regs[16] >> 4) ^ 7) - 8;
    return (unsigned)((v->pal ? 56 : 29) + adjust);
}

void neo_vdp_reset(struct neo_v9990 *v, uint64_t ticks)
{
    for (unsigned i = 0; i < 64; ++i) v->regs[i] = 0;
    v->select = 0xff;
    v->read_buffer = v->pending = 0;
    v->status = 2;
    v->system_reset = v->pal = v->display_enabled = v->vblank_seen = false;
    v->frame_start = v->now = ticks;
    v->fault = NEO_VDP_OK;
    v->fault_register = v->fault_value = 0;
}

void neo_vdp_power_on(struct neo_v9990 *v, uint64_t ticks)
{
    for (unsigned i = 0; i < NEO_VRAM_SIZE; ++i)
        v->vram[i] = (i & 512u) ? 0xff : 0;
    for (unsigned i = 0; i < 256; i += 4) {
        v->palette[i] = 0x9f;
        v->palette[i+1] = v->palette[i+2] = 0x1f;
        v->palette[i+3] = 0;
    }
    neo_vdp_reset(v, ticks);
}

enum neo_vdp_result neo_vdp_tick(struct neo_v9990 *v, uint64_t ticks)
{
    if (v->fault != NEO_VDP_OK) return v->fault;
    if (ticks < v->now) return fail(v, NEO_VDP_TIME_REVERSED, 0, 0);
    v->now = ticks;
    uint64_t period = (v->pal ? 313u : 262u) * NEO_LINE_TICKS;
    if (ticks - v->frame_start >= period) {
        if (!v->vblank_seen) v->pending |= 1;
        v->frame_start += period;
        v->pal = (v->regs[7] & 8) != 0;
        v->display_enabled = (v->regs[8] & 0x80) != 0;
        v->status ^= 2;
        period = (v->pal ? 313u : 262u) * NEO_LINE_TICKS;
        uint64_t skipped = (ticks - v->frame_start) / period;
        if (skipped) v->pending |= 1;
        v->frame_start += skipped * period;
        if (skipped & 1) v->status ^= 2;
        v->vblank_seen = false;
    }
    uint64_t bottom = (top_border(v) + NEO_HEIGHT) * NEO_LINE_TICKS;
    if (!v->vblank_seen && ticks - v->frame_start >= bottom) {
        v->pending |= 1;
        v->vblank_seen = true;
    }
    return NEO_VDP_OK;
}

bool neo_vdp_irq(const struct neo_v9990 *v)
{
    return (v->pending & v->regs[9] & 7) != 0 && v->fault == NEO_VDP_OK;
}

static void palette_next(struct neo_v9990 *v)
{
    uint8_t p = v->regs[14];
    v->regs[14] = (uint8_t)(p + ((p & 3) == 2 ? 2 : (p & 3) == 3 ? -3 : 1));
}

static bool display_supported(const struct neo_v9990 *v)
{
    return (v->regs[6] & 0xf0) == 0x80 &&
           (v->regs[6] & 3) >= 1 && (v->regs[6] & 3) <= 2 &&
           !(v->regs[7] & 6) && !(v->status & 4) &&
           (v->regs[8] & 0x60) == 0x40 && !(v->regs[13] & 0xc0) &&
           !v->regs[17] && !v->regs[18] && !v->regs[19] && !v->regs[20];
}

static enum neo_vdp_result write_register(struct neo_v9990 *v, unsigned reg, uint8_t data)
{
    if (!(access[reg] & 2)) return NEO_VDP_OK;
    if (reg >= 32) {
        if (reg == 52 && (data >> 4) != 0)
            return fail(v, NEO_VDP_UNSUPPORTED_COMMAND, (uint8_t)reg, data);
        v->regs[reg] = data;
        return NEO_VDP_OK;
    }
    data &= write_mask[reg];
    if (reg == 9 && (data & 6))
        return fail(v, NEO_VDP_UNSUPPORTED_IRQ, (uint8_t)reg, data);
    v->regs[reg] = data;
    if (reg == 5) v->read_buffer = v->vram[map_address(v, address(v, 3))];
    if ((v->regs[8] & 0x80) && !display_supported(v))
        return fail(v, NEO_VDP_UNSUPPORTED_DISPLAY, (uint8_t)reg, data);
    return NEO_VDP_OK;
}

enum neo_vdp_result neo_vdp_read(struct neo_v9990 *v, uint16_t port,
                               uint64_t ticks, uint8_t *value)
{
    if (!value || (port & 0xf0) != 0x60) return NEO_VDP_BAD_ARGUMENT;
    enum neo_vdp_result result = neo_vdp_tick(v, ticks);
    if (result != NEO_VDP_OK) return result;
    *value = 0xff;
    switch (port & 15) {
    case 0:
        *value = v->read_buffer;
        if (!v->system_reset && !(v->regs[5] & 0x80)) {
            uint32_t ptr = address(v, 3) + 1;
            set_address(v, 3, ptr);
            v->read_buffer = v->vram[map_address(v, ptr)];
        }
        break;
    case 1:
        *value = v->palette[v->regs[14]];
        if (!v->system_reset && !(v->regs[13] & 0x10)) palette_next(v);
        break;
    case 2:
        return fail(v, NEO_VDP_UNSUPPORTED_COMMAND, 0x62, 0);
    case 3: {
        unsigned reg = v->select & 63;
        if (!v->system_reset && (access[reg] & 1))
            *value = reg >= 53 ? 0 : v->regs[reg];
        if (!v->system_reset && !(v->select & 0x40))
            v->select = (uint8_t)((v->select + 1) & ~0x40);
        break;
    }
    case 5: {
        uint64_t phase = ticks - v->frame_start;
        unsigned y = (unsigned)(phase / NEO_LINE_TICKS);
        unsigned x = (unsigned)(phase % NEO_LINE_TICKS);
        int left = 512 + (((v->regs[16] & 15) ^ 7) - 8) * 8;
        bool hr = (int)x < left || (int)x >= left + 2048;
        bool vr = y < top_border(v) || y >= top_border(v) + NEO_HEIGHT;
        *value = (uint8_t)(v->status | (hr ? 0x20 : 0) | (vr ? 0x40 : 0));
        break;
    }
    case 6: *value = v->pending; break;
    default: break;
    }
    return NEO_VDP_OK;
}

enum neo_vdp_result neo_vdp_write(struct neo_v9990 *v, uint16_t port,
                                uint8_t data, uint64_t ticks)
{
    if ((port & 0xf0) != 0x60) return NEO_VDP_BAD_ARGUMENT;
    enum neo_vdp_result result = neo_vdp_tick(v, ticks);
    if (result != NEO_VDP_OK) return result;
    switch (port & 15) {
    case 0:
        if (!v->system_reset) {
            uint32_t ptr = address(v, 0);
            v->vram[map_address(v, ptr)] = data;
            if (!(v->regs[2] & 0x80)) set_address(v, 0, ptr + 1);
        }
        break;
    case 1: {
        unsigned p = v->system_reset ? 0 : v->regs[14];
        static const uint8_t masks[4] = {0x9f,0x1f,0x1f,0};
        v->palette[p] = v->system_reset ? 0 : data & masks[p & 3];
        if (!v->system_reset) palette_next(v);
        break;
    }
    case 2:
        return fail(v, NEO_VDP_UNSUPPORTED_COMMAND, 0x62, data);
    case 3:
        result = write_register(v, v->select & 63, v->system_reset ? 0 : data);
        if (!(v->select & 0x80))
            v->select = (uint8_t)((v->select & 0xc0) | ((v->select + 1) & 63));
        break;
    case 4: v->select = v->system_reset ? 0 : data; break;
    case 6: v->pending &= (uint8_t)~data; break;
    case 7:
        v->status = (uint8_t)((v->status & ~4) | ((data & 1) << 2));
        if ((data & 2) && !v->system_reset) {
            for (unsigned i = 0; i < 64; ++i) v->regs[i] = 0;
            v->pending = 0;
        }
        v->system_reset = (data & 2) != 0;
        if ((v->regs[8] & 0x80) && !display_supported(v))
            return fail(v, NEO_VDP_UNSUPPORTED_DISPLAY, 0x67, data);
        break;
    default: break;
    }
    return result;
}

static uint32_t rgb(const struct neo_v9990 *v, unsigned index)
{
    unsigned p = 4 * (index & 63);
    unsigned r = v->palette[p] & 31, g = v->palette[p+1], b = v->palette[p+2];
    return (((r << 3) | (r >> 2)) << 16) |
           (((g << 3) | (g >> 2)) << 8) | (b << 3) | (b >> 2);
}

enum neo_vdp_result neo_vdp_render_line(struct neo_v9990 *v, unsigned y,
                                      uint32_t *pixels, size_t count)
{
    if (!pixels || count != NEO_WIDTH || y >= NEO_HEIGHT) return NEO_VDP_BAD_ARGUMENT;
    if (v->fault != NEO_VDP_OK) return v->fault;
    bool enabled = v->display_enabled && (v->regs[8] & 0x80) && !v->system_reset;
    if (enabled && !display_supported(v))
        return fail(v, NEO_VDP_UNSUPPORTED_DISPLAY, 6, v->regs[6]);
    unsigned pitch = 256u << ((v->regs[6] >> 2) & 3);
    for (unsigned x = 0; x < NEO_WIDTH; ++x) {
        unsigned index = v->regs[15] & 63;
        if (enabled) {
            unsigned addr = y * pitch + x;
            if ((v->regs[6] & 3) == 1) {
                uint8_t data = v->vram[bx((addr / 2) & 0x7ffff)];
                index = ((x & 1) ? data & 15 : data >> 4) |
                        ((v->regs[13] & 0x0c) << 2);
            } else {
                index = v->vram[bx(addr & 0x7ffff)] & 63;
            }
        }
        pixels[x] = rgb(v, index);
    }
    return NEO_VDP_OK;
}

enum neo_vdp_result neo_vdp_demo(struct neo_v9990 *v)
{
    uint64_t now = v->now;
    static const uint8_t colors[8][3] = {
        {31,31,31},{31,31,0},{0,31,31},{0,31,0},
        {31,0,31},{31,0,0},{0,0,31},{0,0,0}
    };
    for (unsigned reg = 0; reg < 15; ++reg) {
        enum neo_vdp_result r = neo_vdp_write(v, 0x64, (uint8_t)(0x80 | reg), now);
        if (r != NEO_VDP_OK) return r;
        uint8_t data = reg == 6 ? 0x82 : reg == 8 ? 0xc0 : 0;
        r = neo_vdp_write(v, 0x63, data, now);
        if (r != NEO_VDP_OK) return r;
    }
    for (unsigned i = 0; i < 8; ++i)
        for (unsigned c = 0; c < 3; ++c) {
            enum neo_vdp_result r = neo_vdp_write(v, 0x61, colors[i][c], now);
            if (r != NEO_VDP_OK) return r;
        }
    for (unsigned y = 0; y < NEO_HEIGHT; ++y)
        for (unsigned x = 0; x < NEO_WIDTH; ++x) {
            enum neo_vdp_result r = neo_vdp_write(v, 0x60, (uint8_t)(x / 32), now);
            if (r != NEO_VDP_OK) return r;
        }
    return neo_vdp_tick(v, now + 262u * NEO_LINE_TICKS);
}
