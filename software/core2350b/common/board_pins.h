/*
 * NEO9990 Core2350B pin assignment. Keep in sync with
 * hardware/core2350b/neo9990/gpio-map.csv and tools/design.py.
 *
 * Everything the PIO reads lives in GPIO0-GPIO31 (PIO GPIOBASE 0):
 *
 *   GP0-GP3     A0-A3          with the data bus: `in pins, 12`
 *   GP4-GP11    D0-D7          `out pins, 8` / `out pindirs, 8`
 *   GP12-GP19   HSTX / DVI     (fixed by the RP2350)
 *   GP20-GP25   A4 A5 A6 A7 /M1 /IORQ   port decode field, bit 0 = GP20
 *   GP26 /RD    GP27 /WR
 *   GP28 /INT   GP29 /WAIT   GP30 /BUSDIR   open collector style: the output
 *                                           latch stays 0, the pin direction
 *                                           asserts (output) or releases (input)
 *   GP31 /RESET
 *   GP39        module red LED (470R): never used for the bus
 *   GP47        module PSRAM chip select
 */
#ifndef BOARD_PINS_H
#define BOARD_PINS_H

/* ---- MSX bus ---- */
#define PIN_A0        0
#define PIN_D0        4
#define PIN_A4        20
#define PIN_M1        24
#define PIN_IORQ      25
#define PIN_RD        26
#define PIN_WR        27
#define PIN_INT       28
#define PIN_WAIT      29
#define PIN_BUSDIR    30
#define PIN_RESET     31

/* Module red LED (470R to GND). Not used: a bus line on this pin would light it
 * and load the MSX data bus. */
#define PIN_LED       39

/* Port decode field: `in pins, 6` from PIN_A4 gives A4 A5 A6 A7 /M1 /IORQ with A4 in
 * bit 0. A cycle is ours when /IORQ = 0, /M1 = 1 (not an interrupt acknowledge) and
 * A7-A4 select the port block. */
#define DECODE_V9990          0x16u   /* A7-A4 = 0110 (ports 60h-6Fh): A5, A6, /M1 high */
#define DECODE_V9968          0x18u   /* A7-A4 = 1000 (ports 80h-8Fh): A7, /M1 high */

/* ---- DVI over HSTX: GPIO 12-19 only ---- */
#define PIN_DVI_D0    12
#define PIN_DVI_CLK   14
#define PIN_DVI_D2    16
#define PIN_DVI_D1    18

/* ---- PSRAM (VRAM) ---- */
#define PIN_PSRAM_CS  47

#endif
