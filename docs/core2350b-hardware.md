# Core2350B NEO9990: hardware

NEO9990 rev 1.0 built around a Waveshare Core2350B (RP2350B) module, in
[`hardware/core2350b`](../hardware/core2350b/). The module plugs into the
cartridge board and connects straight to the MSX slot (no level shifters). It
drives HDMI/DVI from the RP2350 HSTX pins and is programmed over USB-C. The
board outline, edge connector, module position and USB-C wiring come from the
PicoVerse 2350 Core2350B board; 

## Project

Open [`neo9990.kicad_pro`](../hardware/core2350b/neo9990/neo9990.kicad_pro) in
KiCad 10.

- [Schematic PDF](../hardware/core2350b/neo9990/plots/schematic.pdf)
- [3D render, front](../hardware/core2350b/neo9990/plots/board-3d.png)
- [3D render, rear](../hardware/core2350b/neo9990/plots/board-back-3d.png)
- [GPIO assignment](../hardware/core2350b/neo9990/gpio-map.csv)

## Design summary

| Block | Parts | Notes |
| --- | --- | --- |
| MSX slot | `EDG1` (PicoVerse edge connector) | Only what a V9990 needs: A0-A7, D0-D7, /IORQ, /M1, /RD, /WR, /RESET, /INT, /WAIT and /BUSDIR. A8-A15, /MREQ, /SLTSL, CLOCK, CS1/2/12, RFSH, SOUNDIN and +/-12 V are not connected. SW1/SW2 slot pins are tied together as on PicoVerse. |
| Module | `U1` Core2350B | Placed rotated 90° counter-clockwise compared with PicoVerse so the HSTX pins (GP12-GP19) sit next to the HDMI connector. |
| Power | `D1` 1N5819, `C1`, `C2` | MSX +5V -> D1 -> module VBUS, USB-C VBUS and HDMI +5V. D1 stops USB power from back-feeding the MSX. |
| USB-C | `J1`, `R4`, `R5`, `SW1` | Same wiring as PicoVerse: D+/D- to the module USB pins, 5.1K CC pull-downs, SW1 = BOOTSEL. |
| Reset | `JP1` | /RESET goes to GP31. Close the (open) solder jumper JP1 to also tie it to the module RUN pin as PicoVerse does. |
| HDMI | `J2` (TE 1747981-2), `R6`-`R13` 270R | One 270R in series with each TMDS line. DDC, CEC and HPD are not connected. |
| Pull-ups | `R1` 2K, `R2` 10K | /WAIT and /BUSDIR to +5V, as on PicoVerse. |

## GPIO map

The map in [`gpio-map.csv`](../hardware/core2350b/neo9990/gpio-map.csv) is laid
out for the PIO bus engine. HSTX owns GP12-GP19, so the bus is split around it:
everything sits in GP0-GP31, the window of a PIO block with the default
`GPIOBASE = 0`.

| GPIO | Signal | PIO use |
| --- | --- | --- |
| GP0-GP3 | A0-A3 | with D0-D7, `in pins, 12` from GP0 gives port and write data |
| GP4-GP11 | D0-D7 | in order, so `out pins, 8` and `out pindirs, 8` drive and release the bus |
| GP12-GP19 | HSTX TMDS lanes | DVI |
| GP20-GP25 | A4 A5 A6 A7 /M1 /IORQ | `in pins, 6` from GP20, compare with `010110b` (V9990): this single test is the port 60h-6Fh decode and also rejects interrupt-acknowledge cycles |
| GP26, GP27 | /RD, /WR | /RD is the `jmp pin`; /WR is a `wait gpio` |
| GP28-GP30 | /INT, /WAIT, /BUSDIR | open-collector style outputs: the output value stays 0 and `pindirs` asserts or releases them |
| GP31 | /RESET | input |
| GP47 | PSRAM chip select | module PSRAM, for VRAM |

GP39 drives the module's red LED through 470R, so the bus does not use it.
Unused GPIOs: GP32-GP38 and GP40-GP46.

**Decode.** There is no decoder chip: the PIO compares A7-A4, /M1 and /IORQ with
`010110b`, which selects the V9990 ports 60h-6Fh. The V9990 keeps VRAM in the
module PSRAM (GP47).

