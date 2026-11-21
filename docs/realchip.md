# Real-chip NEO9990

The NEO9990 hardware implementation based on a real Yamaha V9990, with a
digital-video conversion/output stage for HDMI. It lives in
[`hardware/realchip`](../hardware/realchip/).

## Main project

Open [`neo9990.kicad_pro`](../hardware/realchip/neo9990/neo9990.kicad_pro) in
KiCad. The project uses the local symbol, footprint, and 3D-model libraries
under [`libraries`](../hardware/realchip/neo9990/libraries/).

## Published outputs

- [Schematic](../hardware/realchip/neo9990/neo9990.kicad_sch)
- [PCB](../hardware/realchip/neo9990/neo9990.kicad_pcb)
- [Interactive BOM](../hardware/realchip/neo9990/bom/ibom.html)
- [Fabrication package](../hardware/realchip/neo9990/production/MSX_neo9990_1.0.zip)
- [Production CSV files](../hardware/realchip/neo9990/production/)

The `neo9990-backups` and `production/backups` directories contain historical
project snapshots. Use the current KiCad project and current production
package as the source for any new work.

## Scope

This implementation is independent of the Orange Pi emulation prototype
([Orange Pi NEO9990](orangepi.md)) and of the Core2350B board, and it does not
use any of the software under `software/`.
