NEO9990 PARALLEL HOST FIRMWARE CONTRACT
======================================

neo9990_parallel.h is a portable word decoder, NOT an H618 GPIO driver or
complete emulator. run-tests.ps1 compiles its exhaustive C tests with GCC.
Keep the compiler's runtime DLL directory first on PATH when using MSYS2.
The helper returns explicit error codes; callers must log/report faults and
perform controlled recovery rather than acknowledging invalid data.

Hardware access (native H618 adapter and future backends)
-------------------------------------------------------
An initial native adapter now exists in ../firmware/platform/h618.c.
See ../firmware/README.txt: v0.1 is V9990-only, requires an initialized
framebuffer, and is not board-tested or a full-ROM/OPL4 implementation.
The contract below also covers future backends, not just that limited v1.

All data is PI0-15; PI16 is REQUEST. PH2/3 are RD_N/WR_N; PH4/5 SEL0/1.
PH6 READY; PH7 IRQ_N; PH8 RESET_N input; PH9 slot clock input; UART PH0/1.
Preserve unrelated GPIO bits, configure the data direction as a group, and use
proper volatile MMIO and architecture/device-memory ordering barriers.
Preserve the board's 3.3 V PH/PI GPIO-bank supply configuration when bringing
up the PMIC; do not lower those banks to 1.8 V while 3.3 V CPLD I/O is attached.
Do not copy Linux GPIO timing assumptions into this driver.
Idle data is weakly pulled low. CPLD drives only during RD_N=0, WR_N=1.
Disable RD before data outputs; never drive while reading.
Define and measure strobe/setup/hold/access/turnaround times after vendor fit.

Startup
--------
Set data inputs, RD/WR high, SEL zero, READY low, IRQ_N high.
Wait for MSX RESET_N high; verify register 2 low byte D4.
Load a legal BIOS/ROM image from Pi storage into its DRAM. Keep bootloader/
flash partitions intact. Map files into initialized RAM, not ad-hoc flash
transactions per Z80 fetch.
Initialize ROM mapper and enabled emulators. While READY low, write register
3: page mask bits11:8, feature bits2:0, all other bits zero.
Example 0607 enables pages 1/2 plus ROM/V9990/OPL4 ONLY if all backends exist.
For a ROM-only image use 0602; V9990-only 0601; OPL4-only 0604.
Raise READY after all state is valid. J5 can hold the initial MSX ROM read.
RESET disarms the CPLD; drop READY, reinitialize, configure and rearm on each
reset. Do not carry captured requests/replies across this lifecycle.

Service loop
------------
When PI16 REQUEST is high:
1. Read STATUS (register0), ADDRESS (register1), TICKET (register2).
2. neo9990_decode_request validates reset/error/signature/class flags.
3. ROM: obtain a byte from the current RAM image/mapper, or apply a mapper
   write. V9990/OPL4: perform the ordered emulated I/O access at the correct
   emulated time. Return current state, not a stale asynchronous cache.
4. Write neo9990_reply_word(request.token, result) to register2.
5. Wait for REQUEST low, bounded by a reported timeout/recovery policy.

The data/metadata remain stable during a request. Only one is outstanding.
Read STATUS's sticky error bit even when idle; explicitly clear it via
register0 bit0 after reporting and understanding the fault.
Profile configuration is locked while READY is high. Never echo read-only
BOOT/ARMED bits back into a configuration write.
Mapper banks, ROM bounds, SCC/custom mapper behavior and expanded-slot FFFF
semantics belong to future software, not the current CPLD.
Use an MSX-RAM launcher or preserved bank when replacing an executing ROM.

Concurrency and audio/video
---------------------------
A dedicated bus-service core is a candidate, not a proven performance result.
Only one core/owner may manipulate the parallel GPIO/register interface.
It should own fast ROM lookups and mapper state. Emulator accesses must
preserve a coherent order with renderer/audio workers; FIFO writes alone do
not make immediate status/data reads correct.
Video/audio should use bounded work and well-defined ownership/snapshots or
fences. Do not allow long synthesis/rendering locks to block every ROM fetch.
Measure actual minimum/median/p99/worst WAIT counts under maximum combined
ROM traffic, rendering and audio. Compare to PicoVerse on the same host/turbo
mode before claiming equivalent performance.
The bring-up RTL deliberately adds slot-clock capture/synchronizer/setup
stages. Account for that fixed cost separately from Pi GPIO/RAM latency;
the current interface is not a claim of equal or zero-wait ROM performance.

OPL4 future port
----------------
The PicoVerse 2350 source uses C4-C7 for FM and 7E-7F for PCM. It references
ymfm and 44.1 kHz I2S output, with waveform ROM/sample RAM in its own memory.
The new profile forwards the same ports and supplies an aggregate MSX IRQ.
The RP2350 I2S/PIO DMA backend is not reusable as an H618 HDMI backend.
Implement H618 display/audio clocks, PCM DMA/rings, channel/sample format,
HDMI audio packet/infoframe timing, EDID negotiation and any required
resampling; validate sound and video synchronization on real displays.
This internal HDMI plan needs no external DAC or header I2S connection.
Only synthesized cartridge audio is covered: there is no capture/mixing path
for the MSX's existing PSG/FM audio or its original VDP output.
No synthesizer, sample assets, HDMI driver, BIOS or mapper loader is included.
Review source licenses and supply legally usable sample/ROM data when porting.
