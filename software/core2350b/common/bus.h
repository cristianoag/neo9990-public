#ifndef BUS_H
#define BUS_H

/*
 * NEO9990 MSX bus engine: public interface of common/bus.c.
 */

#include <stdbool.h>
#include <stdint.h>

/* Set up pins, PIO machines and DMA for the MSX bus. `vdp` is the v9990_t in
 * the V9990 firmware and the v9968_t in the V9968 firmware. */
void bus_init(void *vdp);

/* V9990 only: publish P#5 again after the video timing changed one of its
 * bits (called from the core0 interrupt). */
void bus_refresh_status(void);

/* core1 entry point: serves bus events and runs the blitter. */
void bus_core1_main(void);

/* Console "stat": health of core1 and the VDP bus state; "stat reset" clears
 * the counters and maxima. Runs on core0. */
void bus_print_stats(bool reset);

/* Console "vram": CRC32 of every 64KB VRAM block (V9990). */
void bus_print_vram_crc(void);

/* Console "out <port> <value>" (hex): write a port from core0 to test without
 * an MSX. Not synchronised with core1. */
void bus_debug_out(unsigned port, unsigned value);

#endif
