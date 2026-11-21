# Core2350B NEO9990: firmware

Firmware for the Waveshare Core2350B board ([hardware](core2350b-hardware.md)):
an emulation of the Yamaha V9990 (GFX9000 compatible) on ports 60h-6Fh. Video
memory is 512KB: the lower bank in SRAM, the upper bank in the module PSRAM.

The ready-to-flash image, `neo9990-v9990-v1.00.uf2`, is in
[`firmware/`](../firmware/). The sources are in
[`software/core2350b`](../software/core2350b/).

## Source layout

```text
software/core2350b/
  common/    bus engine (bus.pio, bus.c), DVI output, PSRAM driver, board bring-up,
             pin map (board_pins.h), shared CMake files
  core/      the V9990 emulation core
  v9990/     Makefile, CMakeLists.txt, src/main.c
```

## Building

Needs the Raspberry Pi Pico SDK, an Arm GNU toolchain, CMake and (optionally)
Ninja, found in `~/.pico-sdk` (the layout the VS Code Pico extension installs).

```text
cd software/core2350b/v9990
make                  builds and copies the UF2 to <repo>/firmware/
make VERSION=v1.01    another version string
make clean            removes the local build/ folder
```

Override `PICO_SDK_PATH`, `PICO_TOOLCHAIN_PATH`, `CMAKE`, `NINJA` or
`PICOTOOL_DIR` on the command line to use other installations. The UF2 is named
`neo9990-v9990-<version>.uf2` and the version is printed on the USB console at
boot.

## Flashing

Hold BOOTSEL (SW1 on the board), plug in USB-C, and copy the UF2 to the drive
that appears.

## Bus engine

The engine is in `common/bus.pio` and `common/bus.c`. It uses no decoder chip:

- **Pins.** Everything the PIO reads is in GP0-GP31 (`GPIOBASE 0`):
  A0-A3 on GP0-3, D0-D7 on GP4-11, the decode field on GP20-25
  (A4 A5 A6 A7 /M1 /IORQ), /RD and /WR on GP26-27, and /INT, /WAIT, /BUSDIR on
  GP28-30. /RESET is GP31. See the [GPIO map](core2350b-hardware.md#gpio-map).
- **Monitor state machine.** Waits for /IORQ to fall, lets the address and /RD
  settle, then compares the 6-bit decode field with `010110b`. A match is pushed to core1
  through a DMA ring as a snapshot of GP0-GP31. For a read it also pulls /WAIT
  and /BUSDIR low at once and wakes the read state machine.
- **Read state machine.** Waits for core1 to confirm that every earlier access
  has been applied, takes the answer byte from a 16-word table with two DMA
  channels, drives D0-D7, then releases /WAIT, the data bus and /BUSDIR. Reads
  are therefore always answered in order.
- **Open-collector lines.** /INT, /WAIT and /BUSDIR keep their output latch at 0
  and are asserted by switching the pin to an output, and released by switching
  it back to an input.
- **core1.** Applies writes and read side effects, keeps the read table current
  from the chip core, runs the blitter in short slices, drives /INT and handles
  /RESET (an MSX reset resets the emulated chip, not the module).

Two constants in `bus.pio` set the bus timing and can be tuned:
`DECODE_SETTLE` (cycles between /IORQ falling and the decode, so that /RD and
the address are stable) and `WRITE_SETTLE` (extra settling of the data bus before
a write is sampled). The PIO runs at 384MHz, 2.6ns per cycle.

## Video

HSTX drives 640x480@60 DVI on GP12-19 through the 270R resistors. core0 renders
scanlines just ahead of the scan-out; the DVI interrupt also delivers the
emulated chip's vertical blank and line events. The clocks are overclocked to
384MHz (system) and 126MHz (HSTX).

## Console

The USB serial port accepts:

| Command | Meaning |
| --- | --- |
| `stat [reset]` | core1 health, bus event counters, V9990 state, render and DVI timing; `reset` clears the counters |
| `vram` | CRC32 of every 64KB VRAM block (V9990) |
| `out <port> <value>` | write a port without an MSX, in hex |
| `psram [sweep]` | uncached PSRAM test (V9990; destroys VRAM contents) |
