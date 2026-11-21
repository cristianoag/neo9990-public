/* SPDX-License-Identifier: GPL-2.0-only */
#include "h618.h"

#define PIO UINT64_C(0x0300b000)
#define PH (PIO + 7u * 0x24u)
#define PI (PIO + 8u * 0x24u)
#define UART UINT64_C(0x05000000)
#define RD (1u << 2)
#define WR (1u << 3)
#define READY (1u << 6)
#define IRQ (1u << 7)

static uint32_t read32(uint64_t address)
{
    uint32_t result = *(volatile uint32_t *)(uintptr_t)address;
    __asm__ volatile("dsb sy" ::: "memory");
    return result;
}

static void write32(uint64_t address, uint32_t value)
{
    __asm__ volatile("dsb sy" ::: "memory");
    *(volatile uint32_t *)(uintptr_t)address = value;
    __asm__ volatile("dsb sy" ::: "memory");
}

static void update(uint64_t address, uint32_t mask, uint32_t value)
{
    write32(address, (read32(address) & ~mask) | (value & mask));
}

uint64_t h618_counter(void)
{
    uint64_t result;
    __asm__ volatile("isb; mrs %0, cntpct_el0" : "=r"(result));
    return result;
}

uint64_t h618_frequency(void)
{
    uint64_t result;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(result));
    return result;
}

void h618_delay_us(unsigned us)
{
    uint64_t start = h618_counter();
    uint64_t duration = (h618_frequency() * us + 999999u) / 1000000u;
    while (h618_counter() - start < duration) __asm__ volatile("yield");
}

static void putc_uart(char c)
{
    for (unsigned tries = 0; tries < 1000000; ++tries) {
        if (read32(UART+0x14) & 0x20) {
            write32(UART, (uint8_t)c);
            return;
        }
    }
    h618_halt();
}

void h618_puts(const char *text)
{
    while (*text) {
        if (*text == '\n') putc_uart('\r');
        putc_uart(*text++);
    }
}

void h618_hex(uint64_t number)
{
    static const char hex[] = "0123456789abcdef";
    h618_puts("0x");
    for (unsigned i = 16; i; --i) putc_uart(hex[(number >> ((i-1)*4)) & 15]);
}

_Noreturn void h618_halt(void)
{
    for (;;) __asm__ volatile("wfe");
}

static void data_direction(bool output)
{
    write32(PI, output ? 0x11111111 : 0);
    write32(PI+4, output ? 0x11111111 : 0);
}

static uint16_t bus_read(void *context, enum neo9990_register reg)
{
    (void)context;
    update(PH+0x10, RD | WR, RD | WR);
    data_direction(false);
    update(PH+0x10, 3u << 4, (unsigned)reg << 4);
    h618_delay_us(1);
    update(PH+0x10, RD, 0);
    h618_delay_us(1);
    uint16_t result = (uint16_t)read32(PI+0x10);
    update(PH+0x10, RD, RD);
    h618_delay_us(1);
    return result;
}

static void bus_write(void *context, enum neo9990_register reg, uint16_t value)
{
    (void)context;
    update(PH+0x10, RD | WR, RD | WR);
    h618_delay_us(1);
    update(PH+0x10, 3u << 4, (unsigned)reg << 4);
    update(PI+0x10, 0xffff, value);
    data_direction(true);
    h618_delay_us(1);
    update(PH+0x10, WR, 0);
    h618_delay_us(1);
    update(PH+0x10, WR, WR);
    h618_delay_us(1);
    data_direction(false);
    h618_delay_us(1);
}

static void bus_ready(void *context, bool high)
{
    (void)context;
    update(PH+0x10, READY, high ? READY : 0);
}

static void bus_irq(void *context, bool asserted)
{
    (void)context;
    update(PH+0x10, IRQ, asserted ? 0 : IRQ);
}

static bool bus_reset(void *context)
{
    (void)context;
    return (read32(PH+0x10) & (1u << 8)) != 0;
}

struct neo_bus h618_bus_init(void)
{
    /* Preload inactive strobes before changing muxes; preserve UART PH0/PH1. */
    update(PH+0x10, 0xfcu, RD | WR | IRQ);
    data_direction(false);
    update(PI+8, 15u, 0); /* PI16 REQUEST input. */
    update(PH, 0xffffff00u, 0x11111100u);
    update(PH+4, 0xffu, 0); /* PH8 RESET and PH9 clock inputs. */
    return (struct neo_bus){.read=bus_read, .write=bus_write, .ready=bus_ready,
                           .irq=bus_irq, .reset_high=bus_reset};
}
