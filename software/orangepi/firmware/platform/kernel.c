/* SPDX-License-Identifier: GPL-2.0-only */
#include "h618.h"
#include "neo_fdt.h"

static struct neo_v9990 vdp;
static struct neo_service service;
static uint32_t pixels[NEO_WIDTH];
extern char __image_end[];

_Noreturn void neo_exception(uint64_t syndrome, uint64_t pc, uint64_t address)
{
    if (service.bus.ready) neo_service_stop(&service);
    h618_puts("FATAL exception ESR="); h618_hex(syndrome);
    h618_puts(" ELR="); h618_hex(pc);
    h618_puts(" FAR="); h618_hex(address); h618_puts("\n");
    h618_halt();
}

static _Noreturn void fatal(const char *text, uint64_t code)
{
    if (service.bus.ready) neo_service_stop(&service);
    h618_puts("FATAL: "); h618_puts(text); h618_puts(" code="); h618_hex(code);
    h618_puts(" reg="); h618_hex(vdp.fault_register);
    h618_puts(" value="); h618_hex(vdp.fault_value); h618_puts("\n");
    h618_halt();
}

static uint64_t uc_time(uint64_t origin, uint64_t frequency)
{
    uint64_t elapsed = h618_counter()-origin;
    return (elapsed / frequency) * NEO_UC_HZ +
           (elapsed % frequency) * NEO_UC_HZ / frequency;
}

static uint32_t color(uint32_t rgb, bool swap)
{
    if (swap) rgb = ((rgb & 255) << 16) | (rgb & 0xff00) | ((rgb >> 16) & 255);
    return rgb | 0xff000000u;
}

void neo_kernel(uint64_t fdt_address)
{
    h618_puts("\nneo9990 v0.1 native H618 bring-up (GPLv2)\n");
    if (fdt_address < UINT64_C(0x40000000) || (fdt_address & 7) ||
        fdt_address > UINT64_C(0xc0000000)-2u*1024u*1024u ||
        (fdt_address < (uintptr_t)__image_end &&
         fdt_address+2u*1024u*1024u > UINT64_C(0x42000000)))
        fatal("bad FDT address", fdt_address);
    struct neo_boot boot;
    enum neo_boot_result result = neo_fdt_parse((const void *)(uintptr_t)fdt_address,
                                               2u*1024u*1024u, &boot);
    if (result != NEO_BOOT_OK) {
        h618_puts("Requires bootloader-initialized simple-framebuffer.\n"
                  "This image does NOT initialize H618 HDMI/PHY/DRAM/PMIC.\n");
        fatal("FDT/display handoff", result);
    }
    if (neo_fb_overlaps(&boot.fb, UINT64_C(0x42000000),
                        (uintptr_t)__image_end-UINT64_C(0x42000000)) ||
        neo_fb_overlaps(&boot.fb, fdt_address, 2u*1024u*1024u))
        fatal("framebuffer overlaps image or FDT", boot.fb.address);
    uint64_t frequency = h618_frequency();
    if (frequency < 1000000u || frequency > 1000000000u)
        fatal("unexpected architectural counter frequency", frequency);
    uint64_t origin = h618_counter();
    neo_vdp_power_on(&vdp, 0);
    if (!boot.cartridge) {
        if (neo_vdp_demo(&vdp) != NEO_VDP_OK) fatal("demo setup", vdp.fault);
        h618_puts("DEMO: native color bars; cartridge GPIOs remain untouched.\n");
    } else {
        struct neo_bus bus = h618_bus_init();
        neo_service_init(&service, bus, &vdp);
        h618_puts("CARTRIDGE: V9990-only diagnostic subset; remove BOOT-ROM jumper.\n");
    }
    volatile uint32_t *fb = (volatile uint32_t *)(uintptr_t)boot.fb.address;
    for (unsigned y = 0; y < boot.fb.height; ++y)
        for (unsigned x = 0; x < boot.fb.width; ++x)
            fb[(size_t)y*(boot.fb.stride/4)+x] = 0xff101010;
    unsigned line = 0;
    uint64_t demo_offset = boot.cartridge ? 0 : vdp.now;
    for (;;) {
        uint64_t ticks = uc_time(origin, frequency)+demo_offset;
        if (boot.cartridge) {
            enum neo_service_result state = neo_service_poll(&service, ticks);
            if (state > NEO_SERVICE_REPLIED) fatal("CPLD/service", state);
        } else if (neo_vdp_tick(&vdp, ticks) != NEO_VDP_OK) fatal("VDP time", vdp.fault);
        if (neo_vdp_render_line(&vdp, line, pixels, NEO_WIDTH) != NEO_VDP_OK)
            fatal("unsupported V9990 display", vdp.fault);
        unsigned xoff = (boot.fb.width-NEO_WIDTH)/2;
        unsigned yoff = (boot.fb.height-NEO_HEIGHT)/2;
        size_t offset = (size_t)(line+yoff)*(boot.fb.stride/4)+xoff;
        for (unsigned x = 0; x < NEO_WIDTH; ++x)
            fb[offset+x] = color(pixels[x], boot.fb.swap_red_blue);
        __asm__ volatile("dsb sy" ::: "memory");
        line = (line+1) % NEO_HEIGHT;
    }
}
