# Orange Pi NEO9990

The experimental NEO9990 implementation built around an Orange Pi Zero 2W and
an EPM7160STC100-10 CPLD bus frontend. Hardware is in
[`hardware/orangepi`](../hardware/orangepi/), the supporting software in
[`software/orangepi`](../software/orangepi/).

The design uses the MSX cartridge bus to forward V9990, ROM, and optional OPL4
accesses to software running on the Orange Pi. It is a prototype, not a
finished cartridge or a replacement for the real-chip implementation
([Real-chip NEO9990](realchip.md)).

## Hardware

Open [`neo9990.kicad_pro`](../hardware/orangepi/neo9990/neo9990.kicad_pro) in
KiCad. Start with
[`prototype-notes.txt`](../hardware/orangepi/neo9990/prototype-notes.txt) for
the electrical restrictions, assembly notes, current validation, and release
gates.

- [GPIO assignment](../hardware/orangepi/neo9990/gpio-map.csv)
- [BOM](../hardware/orangepi/neo9990/bom.csv)
- [Assembly and 3D outputs](../hardware/orangepi/neo9990/plots/)
- [Orange Pi model notes](../hardware/orangepi/neo9990/models/model-notes.txt)

## Software

The CPLD sources, host protocol and firmware are the supporting code and tests
for the Orange Pi board only. They are not used by the real-chip board or the
Core2350B board.

### CPLD frontend

[`cpld`](../software/orangepi/cpld/) implements the EPM7160 bus frontend,
configuration profiles, parallel mailbox, reset/ready handling, and the Quartus
project. See [`cpld/README.txt`](../software/orangepi/cpld/README.txt) for the
protocol, profiles, toolchain, and release requirements.

### Host protocol

[`host`](../software/orangepi/host/) provides the portable C decoder for the
parallel bus words and exhaustive protocol tests. It is a contract/helper
library, not an H618 GPIO driver. See
[`host/README.txt`](../software/orangepi/host/README.txt).

### Bare-metal firmware

[`firmware`](../software/orangepi/firmware/) contains the freestanding AArch64
V9990 bring-up image, native host adapter, tests, and openMSX-derived V9990
code. See [`firmware/README.txt`](../software/orangepi/firmware/README.txt)
before building or booting it.

## Limitations

The firmware currently requires an externally initialized framebuffer. It is
not board-tested, self-booting, or HDMI-complete, and it does not provide a
complete HDMI driver, ROM/mapper implementation, OPL4 synthesis, or
demonstrated cartridge operation. The current code is test and bring-up
infrastructure rather than a complete GFX9000, ROM/mapper, OPL4, or HDMI
implementation. The component notes above contain the explicit limitations and
safety gates.
