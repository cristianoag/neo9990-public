#ifndef PLATFORM_H
#define PLATFORM_H

/*
 * NEO9990 board bring-up (platform.c): bus pin release, clocks, console.
 */

#include <stdint.h>

/* Firmware version string, set by the build (-DNEO_FW_VERSION=...). */
#ifndef NEO_FW_VERSION
#define NEO_FW_VERSION "dev"
#endif

/* First thing main() does. Turns every MSX bus pin into a released input with
 * no pull: the RP2350 pads wake up with pull-downs that would otherwise hold
 * /INT, /WAIT and /BUSDIR low, and the MSX has to keep running even when the
 * emulation never comes up (for example with no PSRAM). */
void neo_release_bus_outputs(void);

/* Raise the clocks: 384MHz system, 126MHz HSTX, 48MHz USB. Call before USB or
 * PIO are started. */
void neo_init_clocks(void);

/* Line-based USB console. Handles "stat [reset]", "vram", "out <port> <val>",
 * "psram [sweep]" (V9990 only) and "help"; any other line goes to `chip_cmd`
 * (may be NULL), which returns non-zero when it consumed it. Poll once per
 * frame. */
void neo_console_poll(int (*chip_cmd)(const char *line));

#endif
