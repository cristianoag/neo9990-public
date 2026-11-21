NEO9990 v0.1 - NATIVE V9990 BRING-UP
===================================

Status: builds and runs in AArch64 instruction emulation. NOT board-tested.
This is a bare-metal program, not a Linux application. It is NOT a complete
GFX9000 implementation and is NOT a self-booting microSD image.

IMPORTANT HDMI BLOCKER
----------------------
There is no native H618 display-engine/TCON/HDMI-PHY initialization driver
in this version. A blank Orange Pi with mainline U-Boot will NOT show this
program on HDMI merely by copying Image to a card. The current mainline
orangepi_zero2w_defconfig does not supply a ready HDMI framebuffer.

The image accepts a real, already initialized simple-framebuffer handoff.
It does not create a framebuffer node to pretend HDMI is running. Without
that handoff it logs a precise error on UART and stops, without touching
the cartridge GPIOs. Implementing and physically validating native H618
HDMI initialization is the next board-bring-up task.

Implemented
------------
- Freestanding Cortex-A53 AArch64 Image with Linux arm64 BOOT PROTOCOL header;
  this describes the loader ABI only. No Linux kernel or runtime is used.
- EL2 -> EL1 entry, EL1 entry, exception diagnostics, stack/BSS, fixed storage.
- Portable openMSX-derived register/port subset and 512 KiB emulated VRAM.
- P1/P2/Bx CPU address transformations, buffered VRAM reads, independent
  read/write addresses, wrap/inhibit, register auto-increment, palette masks
  and stepping, soft reset retaining VRAM/palette.
- B1 noninterlaced 256x212 bitmap output: BP4 (4 bpp, palette bank) and BP6
  (8-bit storage with 6-bit palette indices). Image pitch 256/512/1024/2048.
  Sprites/cursors must be disabled (R8 bit6=1), scrolling zero.
- Explicit UC time, approximate NTSC/PAL frame/blanking and vertical IRQ.
- UART diagnostics and an inherited 32-bit RGB/BGR framebuffer writer.
- Default isolated color-bar demo. In demo mode NO cartridge GPIO is touched.
- Opt-in rev-D CPLD D4 mailbox adapter: V9990-only feature mask, safe data-bus
  turnaround, token replies, error/ACK timeout handling and reset rearm,
  including a reset pulse missed by polling but retained in CPLD ARMED.

Not implemented / intentionally refused
---------------------------------------
Command engine except STOP, command-data transfers, P1/P2 DISPLAY, other Bx
display modes, sprites/cursors, scrolling, YJK/YUV/direct-color, interlace,
horizontal/command IRQ, superimposition and raster-accurate rendering.
Unsupported command/data accesses, unsupported active display configurations
and unsupported IRQ enables produce explicit faults; the bus adapter drops
READY/IRQ and does not acknowledge the failing request with invented data.
Subsequent cartridge I/O is no longer claimed until restart/recovery.

No ROM BIOS/loader/mapper, OPL4 synthesis, sample ROM, HDMI audio, SD driver,
SPL/DRAM training, PMIC/clock initialization or complete openMSX scheduler.
No native HDMI link setup, EDID or display power-sequencing support.
No zero-wait/PicoVerse-equivalent performance or hardware-validation claim.
Frame timing is provisional: register changes are not scanline scheduled;
display adjust is used for status timing, not a full border rasterizer.
The scanline writer is unsynchronized to scanout and can tear. Framebuffer
content and emulator state are owned by one core; no multicore races/locks.

License and provenance
-----------------------
COPYING contains GPLv2. New code here and the shared host protocol header
use GPL-2.0-only; the hardware license remains unchanged.
third_party/openmsx/PROVENANCE.txt records the exact upstream commit,
original files and adaptations. Original reference source and author credits
are included. No proprietary BIOS, font, wave ROM or game data is bundled.
When distributing a derived binary, also provide corresponding source,
build scripts and license/notices as required by GPLv2. Do not apply the
repository hardware's NonCommercial restriction to this GPL firmware.

Build (Windows)
----------------
Requirements: Python 3, LLVM clang + ld.lld. No C library, compiler runtime,
SDK, proprietary toolchain or board access is needed to build.
GNU Make is needed for the Makefile targets.

From the repository root, with clang and ld.lld on PATH:
  make -C software\orangepi\firmware

This builds and checks the image, then copies it to:
  firmware\neo9990-orangepi-v0.1.bin

The versioned .bin is a byte-for-byte copy of build\Image, not a UF2 or a
microSD disk image. Use it as Image in the loader example below, or adjust
the fatload filename. It still requires the initialized framebuffer handoff.
The default VERSION=v0.1 matches the current firmware banner; overriding
VERSION changes the output filename only. PYTHON, CLANG and LD_LLD can be
overridden with tool names or full executable paths.

  make -C software\orangepi\firmware build
    Build locally without copying to the root firmware folder.
  make -C software\orangepi\firmware clean
    Remove only the local build directory; published files are kept.

The original builder also remains available, for example with MSYS2 LLVM:
  python software\orangepi\firmware\build.py --cc C:\msys64\ucrt64\bin\clang.exe --ld C:\msys64\ucrt64\bin\ld.lld.exe

Outputs (ignored build directory):
  build\Image          raw bootloader-handoff image, not a disk image
  build\neo9990.elf    symbols/debug information
  build\neo9990.map    fixed memory layout

The builder verifies arm64 boot header, entry 0x42000000, machine type and
RAM size. It never scans disks, flashes storage or changes boot partitions.
On other systems: python3 build.py with clang/ld.lld on PATH.

Tests
------
The Makefile runs both the firmware and shared host-protocol C suites:
  make -C software\orangepi\firmware test HOST_CC=C:\msys64\ucrt64\bin\gcc.exe

HOST_CC defaults to gcc on PATH. POWERSHELL can select another PowerShell
executable. The original test runners remain available:
  .\software\orangepi\firmware\run-tests.ps1
  .\software\orangepi\host\run-tests.ps1

Optional native instruction-emulation tests:
  python -m pip install -r software\orangepi\firmware\tests\requirements.txt
  python software\orangepi\firmware\tests\test_native.py
Or rebuild and execute the actual image through Make:
  make -C software\orangepi\firmware test-native

The C suite sweeps ALL 512 KiB in P1/P2/Bx, tests physical byte placement,
buffering, wrap/inhibit, palette, selection, reset, VBlank/PAL, every demo
pixel, mailbox token wrap/duplicates, reset rearm, faults and FDT validation.
Pass -Preview <path.ppm> to run-tests.ps1 to write the generated test pattern.
It contains 256x212 pixels in eight vertical color bars.

The optional Unicorn tests execute the actual cross-linked Image at EL1
and EL2, compare all 54,272 framebuffer pixels and check missing-framebuffer
and missing-CPLD failure paths. A CPLD mailbox model additionally checks native
GPIO strobe/direction ordering, preserved mux bits, V9990-only configuration,
four token replies and rearm after a reset pulse missed by polling.
Only timer/MMIO behavior is modeled. These
tests DO NOT model the H618 HDMI hardware, CPLD propagation, power or GPIO
electrical behavior; they do not establish physical operation.

Current validation: 6,874,484 C checks pass with GCC and clang. With preview
file validation enabled, 6,928,972 checks pass. All five native image tests
pass. No actual Orange Pi/MSX/HDMI hardware result is claimed.

Loader / display handoff contract
---------------------------------
Target: Orange Pi Zero 2W H618, 2 GiB RAM, original orientation, rev-D board.
An external boot chain must initialize DRAM/PMIC/clocks, UART0 PH0/PH1,
architectural counter and HDMI scanout before entering this image.
UART0 is inherited at 0x05000000; configure 115200 8N1 in the loader.
The firmware preserves PH0/PH1 mux and does not reprogram the PMIC.
Both PH/PI banks MUST stay at 3.3 V, not 1.8 V.

Use the arm64 Linux boot protocol (e.g. U-Boot booti), not U-Boot go:
  primary CPU only, little endian, nonsecure EL1h or EL2h;
  MMU and data cache off; prior dirty caches flushed by loader;
  x0 = physical FDT pointer, x1-x3 zero, no active asynchronous DMA except
  the already initialized display, secondary CPUs parked.

Image address: 0x42000000. Reserve 0x42000000-0x43000000 for image/BSS/stack.
FDT example load address: 0x4fa00000, 8-byte aligned, valid FDT v17.
Keep it and its 2 MiB validation window outside the image/framebuffer.
There must be one enabled simple-framebuffer under /chosen or root, whose
physical address/size describe REAL reserved scanout memory. Width >=256,
height >=212, max4096x2160, 4-byte-aligned address/stride, sufficient region.
Accepted formats: x8r8g8b8, a8r8g8b8, x8b8g8r8, a8b8g8r8.
The framebuffer must lie in actual 2 GiB DRAM (0x40000000-0xc0000000).
Nonempty address translations and multiple enabled framebuffers are refused.
Under /chosen, explicitly set #address-cells/#size-cells to match reg cells.
Root compatible must include "xunlong,orangepi-zero2w".

/chosen property neo9990,mode:
  absent or "demo": isolated color bars, no GPIO service
  "cartridge": opt in to real cartridge GPIOs; requires all hardware gates
  anything else: boot error

Do not invent a framebuffer address and add an FDT node: the display engine
must already be programmed to scan that memory, with clocks/PHY kept running
across the loader's handoff. U-Boot may disable display devices on OS entry;
verify this explicitly when implementing a real loader/display path.

Serial-only boot test on a SPARE prepared boot card
--------------------------------------------------
Prepare/validate a trusted H618 Orange Pi Zero 2W SPL + TF-A + U-Boot first.
This project does not yet build or supply that external boot chain.
Do not overwrite your working card. No raw-disk flashing script is provided.
With Image and the board's matching DTB on a FAT partition, illustrative
U-Boot commands are:
  fatload mmc 0:1 0x42000000 Image
  fatload mmc 0:1 0x4fa00000 sun50i-h618-orangepi-zero2w.dtb
  booti 0x42000000 - 0x4fa00000

Check file sizes/addresses against your loader's reservations. These commands
load into RAM and do not write flash. With a normal mainline DTB and no HDMI
initialization, EXPECT "FATAL: FDT/display handoff" on UART. That is a useful
startup test, NOT a successful video test. No fabricated HDMI output claim.

Real cartridge safety and measurement
--------------------------------------
Do not enable cartridge mode before the fixed-pin CPLD fit passes the
timing/power/electrical/mechanical checks in the hardware notes. The supplied
v0.1 POF is an experimental fit without SDC timing constraints; it does NOT
satisfy those gates. An unprogrammed CPLD cannot provide deterministic protection.
Remove BOOT-ROM J5 with power OFF: this firmware has no ROM backend.
First test Pi/display alone, then an isolated powered board fixture with
defined MSX input levels. Only then consider connecting an MSX/expander.
Use the verified high-current slot supply; never simultaneously feed USB-C
or USB VBUS while slot-powered. No hot plugging.

Diagnostic GPIO strobes use conservative 1 us setup/access/hold delays and
full device barriers. PI0-15 direction switches together; RD is disabled
before Pi drives, WR rising captures, outputs release before CPLD reads.
Other GPIO bits and UART mux are preserved; PC12 is untouched.
All bus GPIO operations have one owner. IRQ is active low at PH7.
The polling adapter checks sticky errors even when idle; a 100 ms reply
completion timeout reports a fault rather than replaying a request.
Fatal errors release READY/IRQ and print a code/register/value on UART.

Capture PH2/PH3/PH6, PI data/REQUEST, MSX WAIT/data/RESET with a logic analyzer.
Measure all voltage levels, turnaround, setup/hold, RESET rearm, startup
supply sequence, total current and WAIT min/median/p99/worst. Rendering uses
uncached memory and can significantly delay bus service. Hardware ROM mode
and arbitrary MSX programs must not be treated as supported by this v1.

Public platform references
--------------------------
https://github.com/u-boot/u-boot/blob/master/configs/orangepi_zero2w_defconfig
https://github.com/torvalds/linux/blob/master/arch/arm64/boot/dts/allwinner/sun50i-h616.dtsi
https://github.com/torvalds/linux/blob/master/arch/arm64/boot/dts/allwinner/sun50i-h618-orangepi-zero2w.dts
https://github.com/torvalds/linux/blob/master/drivers/pinctrl/sunxi/pinctrl-sun50i-h616.c
https://github.com/torvalds/linux/blob/master/drivers/pinctrl/sunxi/pinctrl-sunxi.h
https://docs.kernel.org/arch/arm64/booting.html
