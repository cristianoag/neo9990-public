/*
 * NEO9990 V9990 firmware (GFX9000 compatible video cartridge).
 *
 * Work split
 *   core0 : DVI scan-out interrupt, scanline renderer, USB console
 *   core1 : MSX bus events and the blitter (common/bus.c)
 *   PIO   : port decode, /WAIT + /BUSDIR, data bus (common/bus.pio)
 * Video memory: 512KB. The lower 256KB bank sits in SRAM, the upper bank in
 * the Core2350B PSRAM (GP47) and reaches the renderer through DMA bursts.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/sync.h"
#include "hardware/timer.h"
#include "board_pins.h"
#include "bus.h"
#include "dvi_out.h"
#include "platform.h"
#include "psram.h"
#include "v9990.h"
#include "v9990_vram.h"

static v9990_t v9990;
/* Lower VRAM bank, kept in SRAM. Frequently used data (layer A patterns,
 * sprites) and half of the blitter traffic therefore never touch the PSRAM. */
static uint8_t vram0[V9990_VRAM_SIZE / 2] __attribute__((aligned(4)));
static dvi_vdp_t video;

/* ---- video callbacks ---- */

static unsigned __not_in_flash_func(v9990_height)(const void *v) { return v9990_display_height(v); }
static void __not_in_flash_func(v9990_vbs)(void *v) { v9990_video_vblank_start(v); }
static void __not_in_flash_func(v9990_vbe)(void *v) { v9990_video_vblank_end(v); }
static void __not_in_flash_func(v9990_line)(void *v, unsigned n) { v9990_video_line_start(v, n); }

/* Horizontal blank bit (P#5 bit 5). Programs spin on it, for instance inside
 * a line interrupt with interrupts masked, so it has to toggle for real. It is
 * pushed to the read table immediately instead of waiting for core1's next
 * loop pass. */
static void __not_in_flash_func(v9990_hblank)(void *v, bool on) {
    v9990_t *p = v;
    if (on) V9990_SET_BITS(p->status, V9990_ST_HR);
    else    V9990_CLR_BITS(p->status, V9990_ST_HR);
    bus_refresh_status();
}

/* PSRAM rows reach the renderer as single DMA bursts rather than one cache
 * miss every 8 bytes. While a line is decoded, the renderer starts the burst
 * for the next line's upper-bank row and collects it on the following call. */
static bool pf_busy;                  /* a burst is in flight */

static void __not_in_flash_func(v9990_prefetch_wait)(const v9990_t *v) {
    (void)v;
    if (pf_busy) { psram_stream_wait(); pf_busy = false; }
}

static bool __not_in_flash_func(v9990_prefetch)(const v9990_t *v, uint32_t phys,
                                                uint8_t *dst, uint32_t len) {
    if (!(phys & 0x40000u)) return false;
    v9990_prefetch_wait(v);
    pf_busy = psram_stream_start(dst, v->vram1 + (phys & 0x3FFFFu), len);
    return pf_busy;
}

static void __not_in_flash_func(v9990_fetch)(const v9990_t *v, uint32_t phys,
                                             uint8_t *dst, uint32_t len) {
    if (phys & 0x40000u) {
        v9990_prefetch_wait(v);
        psram_stream_read(dst, v->vram1 + (phys & 0x3FFFFu), len);
    } else {                                     /* lower bank, SRAM: word copy (memcpy lives in flash) */
        const uint32_t *s = (const uint32_t *)(v->vram + phys);
        uint32_t *d = (uint32_t *)dst;
        for (uint32_t i = 0; i < len / 4u; i++) d[i] = s[i];
    }
}

static void __not_in_flash_func(set_border)(uint16_t bd) {
    for (uint i = 0; i < DVI_WIDTH; i++) dvi_lines.border.px[i] = bd;
}

/* Keep the renderer a few lines ahead of the DVI scan-out. */
static void __not_in_flash_func(render_loop)(void) {
    uint32_t frame = dvi_lines.frame;
    uint32_t next = 0;

    for (;;) {
        uint32_t f = dvi_lines.frame;
        if (f != frame) {
            frame = f;
            next = 0;
            set_border(v9990_backdrop_rgb565(&v9990));
            neo_console_poll(NULL);
        }
        /* Which VDP line is on screen right now (negative above the picture).
         * Below the picture the next frame has to wait for vertical blank. */
        int32_t pos = dvi_lines.pos, scale = (int32_t)dvi_lines.scale;
        int32_t height = (int32_t)dvi_lines.src_height;
        int32_t cur = pos >= height * scale ? -DVI_LINE_BUFFERS - 1
                    : pos >= 0 ? pos / scale : -((-pos + scale - 1) / scale);
        if (next < dvi_lines.src_height &&
            (int32_t)next <= cur + (DVI_LINE_BUFFERS - 1)) {
            if ((int32_t)next <= cur) dvi_lines.render_late++;
            uint32_t t0 = timer_hw->timerawl;
            v9990_render_line(&v9990, next,
                              dvi_lines.line[next & (DVI_LINE_BUFFERS - 1)].px,
                              DVI_WIDTH);
            uint32_t dt = timer_hw->timerawl - t0;
            dvi_lines.render_lines++;
            dvi_lines.render_us += dt;
            if (dt > dvi_lines.render_max_us) dvi_lines.render_max_us = dt;
            next++;
        }
    }
}

int main(void) {
    neo_release_bus_outputs();
    neo_init_clocks();
    stdio_init_all();

    printf("NEO9990 V9990 " NEO_FW_VERSION ", clk_sys %lu Hz\n",
           (unsigned long)clock_get_hz(clk_sys));

    size_t psram_size = psram_init();
    printf("PSRAM %u KB\n", (unsigned)(psram_size / 1024));
    if (psram_size < V9990_VRAM_SIZE) {
        /* Keep the halted state visible on the console: the boot banner is
         * long gone by the time a terminal is attached. The MSX bus stays
         * unanswered, so the computer still boots. */
        for (absolute_time_t t = get_absolute_time();;) {
            if (time_reached(t)) {
                printf("PSRAM not found (%u KB): V9990 halted, the MSX bus is not answered\n",
                       (unsigned)(psram_size / 1024));
                t = make_timeout_time_ms(1000);
            }
            neo_console_poll(NULL);
        }
    }

    v9990_init_banks(&v9990, vram0, PSRAM_BASE);
    psram_stream_init();
    v9990.vram_fetch = v9990_fetch;
    v9990.vram_prefetch = v9990_prefetch;
    v9990.vram_prefetch_wait = v9990_prefetch_wait;
    bus_init(&v9990);
    video = (dvi_vdp_t){ &v9990, v9990_height, v9990_vbs, v9990_vbe, v9990_line,
                         v9990_hblank, false };

    multicore_launch_core1(bus_core1_main);
    dvi_out_start(&video);
    render_loop();
}
