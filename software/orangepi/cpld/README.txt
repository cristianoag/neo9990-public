NEO9990 REV D - PARALLEL ROM / V9990 / OPL4 FRONTEND
=================================================

Target: EPM7160STC100-10, 160 macrocells, TQFP100, core 5 V / I/O 3.3 V.
This is frontend HDL, not a complete cartridge firmware. The v0.1 POF fits
with Quartus II 11.0 SP1: 144 of 160 macrocells, with all fixed pins preserved.
Timing closure and physical validation remain required. The project has no
SDC constraints; Quartus reports critical timing warnings using derived
1 ns clocks. Compilation success is NOT timing sign-off or board validation.

Bus connections
---------------
MSX A0-A15, D0-D7, MREQ, SLTSL, IORQ, RD, WR, M1, RESET, CLOCK are connected.
WAIT/INT/BUSDIR are open-drain outputs (HDL 0/Z plus Quartus assignments).
The host link is a 16-bit bidirectional PARALLEL bus, NOT the former SPI ABI.
Pi PI0-PI15 map directly to HOST_D0-D15. PI16 receives HOST_REQUEST.
PH0/PH1 = UART0 TX/RX; PH2/PH3 = HOST_RD_N/HOST_WR_N.
PH4/PH5 = HOST_SEL0/HOST_SEL1; PH6 = HOST_READY; PH7 = HOST_IRQ_N.
PH8 = HOST_RESET_N; PH9 = HOST_MSX_CLOCK. PC12 is spare/unconnected.
Four isolated 4x100k resistor networks bias idle host data low.
RD/WR pull high, SEL0/1 pull low, READY pulls low, IRQ_N pulls high.

VCCINT 39/91 = fused 5 V; VCCIO 3/18/34/51/66/82 = Pi-derived 3.3 V.
GND 11/26/38/43/59/74/86/95.
JTAG TDI=4, TMS=15, TCK=62, TDO=73 remains reserved; Vref/pullups are 3.3 V.
HOST_WR_N uses dedicated clock input 87; READY/RESET/MSX CLOCK use 88/89/90.
pins.json is the source for PCB AND Quartus assignments. 61 functional pins
are allocated, with 19 available pins unused. Do not edit QSF pins separately.
All Pi-bound signals originate at 3.3 V CPLD I/O, including forwarded RESET
and CLOCK. Raw 5 V MSX nets never connect to Pi GPIOs.

Profiles and decode
-------------------
Feature bit 0: V9990, low I/O ports 60-6F.
Feature bit 1: ROM/memory, MREQ=0, SLTSL=0, IORQ=1, enabled 16K page.
Feature bit 2: OPL4, low I/O ports C4-C7 and 7E-7F.
I/O requires IORQ=0, MREQ=1, M1=1. Memory accepts opcode fetches with M1=0.
Exactly one of RD/WR must be low. Refresh/no-strobe cycles are ignored.
Memory high-address bits are preserved, not truncated to a port number.
ROM page bits 0/1/2/3 select 0000-3FFF / 4000-7FFF / 8000-BFFF / C000-FFFF.
Default page mask is 6 (pages 1 and 2). Features can be combined.
Do not enable a feature without a working firmware backend.
This is request forwarding; it does NOT implement ROM storage, mapper banks,
V9990 rendering, OPL4 synthesis or HDMI audio in the CPLD.

BOOT-ROM jumper
---------------
J5 installed pulls BOOT_ROM high. Until host configuration overrides it,
ROM mode defaults on and the first selected ROM access holds WAIT while the
Pi initializes its DRAM and loads a BIOS image. Data remains high impedance.
J5 removed: no ROM claim/startup wait until firmware explicitly enables it.
V9990/OPL4 default off. Change J5 only with all power off.
If the Pi fails to become ready in boot-ROM mode, MSX may wait indefinitely.
No timeout fabricates a BIOS byte or silently acknowledges a missing backend.
This hold requires valid CPLD core AND I/O supplies; verify their rise time
before the MSX's cartridge scan. It is not a power-failure guarantee.

Parallel register ABI - signature D4
------------------------------------
SEL=0 read: STATUS
  bit15 pending request; bit14 read operation; bit13 memory operation;
  bit12 raw MSX RESET_N; bit11 sticky protocol error;
  bits10:8 class (1 V9990, 2 ROM, 3 OPL4); bits7:0 captured write byte.
SEL=0 write: bit0=1 explicitly clears the sticky protocol error.
SEL=1 read: full captured 16-bit MSX address. Writes set protocol error.
SEL=2 read: bits15:8 request sequence token; bits7:0 signature D4.
SEL=2 write: bits15:8 matching token; bits7:0 read result (ignored for writes).
  A valid write commits the reply. Wrong/stale/duplicate/unarmed commits set
  protocol error and do not acknowledge or overwrite an in-flight reply.
SEL=3 read: bit15 BOOT jumper, bit14 armed, bits11:8 ROM page mask,
  bits2:0 feature enables; other bits zero.
  Armed is the reset-rearm latch; effective readiness is READY AND armed.
SEL=3 write: bits11:8 ROM page mask, bits2:0 features; all other bits MUST be
  zero. Allowed only while HOST_READY=0. Live/invalid writes set error.

The request fields/token are stable until acknowledged/cycle end. The host
must monitor RESET and not use a captured request across reset/reconfiguration.
The sequence wraps after 256 requests; it is not an unlimited generation ID.
The interface permits ONE outstanding access, not a FIFO.

Host electrical transaction contract
------------------------------------
Idle: RD_N=WR_N=1. Deassert RD BEFORE configuring/driving any host data output.
READ: data GPIOs inputs; set SEL; assert RD_N; wait required access time;
sample PI0-15; deassert RD_N. Do not change SEL while RD_N is asserted.
WRITE: RD_N high; configure data outputs; establish SEL/data; pulse WR_N low
then high. Data latches at the rising edge. Respect setup/hold, then release
data to inputs before a read. Never assert RD_N and WR_N together.
Overlapping strobes tristate CPLD data and report protocol error.
Use memory/peripheral barriers and measured delays in the H618 backend.
Do not guess that a CPU instruction cycle equals a GPIO transaction.
The implementation of actual MMIO/pin mux/cache maintenance is future firmware.

Servicing a request
------------------
Poll REQUEST in PI16 (same bank as data), read STATUS/ADDRESS/TICKET, validate
pending/reset/error/signature/class, then dispatch to the correct backend.
ROM bytes come from a previously loaded Pi-RAM image and software mapper.
Mapper writes must update state before ACK. Implement any expanded-slot
semantics if firmware enables page 3/FFFF. No external SRAM is fitted.
Write {token,result} to SEL=2, then wait for REQUEST low before another reply.
The CPLD crosses the completion toggle through two MSX-clock synchronizers.
It drives registered read data for a full slot clock BEFORE releasing WAIT.
Do not modify a reply until consumed. Errors require explicit logging/recovery.
The pure C decoder in ../host checks the word ABI; it is not a GPIO driver.

Startup/reset sequence
----------------------
Initialize data GPIOs as inputs and control strobes high; set READY low and
IRQ_N high. Wait for RESET_N high and verify signature D4.
Load BIOS/ROM images, initialize mapper/emulator state, configure profile/page
mask, then raise READY. An outstanding boot-ROM read is then captured/served.
An MSX reset clears the armed latch even if READY remains high. The host MUST
drop READY, reset/reinitialize its backends, configure again, then raise READY.
With BOOT jumper installed, ROM access waits for this rearm.
READY low resets bus/reply state but preserves configuration; MSX reset clears
configuration overrides. Only change configuration while READY is low.
Never overwrite an executing ROM image: use a RAM-resident MSX launcher or
preserve an execution bank and coordinate image/map switching.

V9990 / OPL4 / HDMI
-------------------
The bus service path needs bounded latency while rendering/audio run.
PicoVerse 2350 opl4_main.c and msx_opl4.pio confirm C4-C7 / 7E-7F decoding.
Its ymfm state ownership, timer/status semantics, sample buffering and IRQ
behavior are useful references; do not assume its RP2350 I2S DMA backend works
on H618. A bare-metal H618 HDMI audio/DMA/clock backend is NOT implemented here.
No header I2S pins or extra DAC are required by the planned internal HDMI path.
Future firmware must supply licensed sample data and its own RAM buffers.
No YRW801 image or copied PicoVerse firmware/sample binary is bundled.

Build (Windows)
----------------
Requirements: GNU Make, Python 3 and legacy Quartus II with MAX7000S support.
Quartus II 13.0 SP1 is the recommended legacy release; the installed 11.0 SP1
was used for the v0.1 prototype fit. Current Quartus Prime does not target
MAX7000S.

From the repository root:
  make -C software\orangepi\cpld QUARTUS_SH=C:\altera\11.0sp1\quartus\bin\quartus_sh.exe

QUARTUS_SH defaults to quartus_sh on PATH; override it for another supported
installation. The Makefile regenerates QSF/QPF from pins.json before every
compile, then copies the assembled programming file to:
  firmware\neo9990-orangepi-cpld-v0.1.pof

VERSION=v0.1 controls the output filename, not the D4 protocol signature.
PYTHON and POWERSHELL can also be overridden with executable paths.
  make -C software\orangepi\cpld generate
    Regenerate assignments only.
  make -C software\orangepi\cpld build QUARTUS_SH=<executable>
    Compile locally without copying the POF.
  make -C software\orangepi\cpld clean
    Remove local db, incremental_db and output_files; published files are kept.

WAIT/INT/BUSDIR use AUTO_OPEN_DRAIN assignments for their constant-low/tri-state
HDL drivers. The fitted pin report confirms open-drain mode on all three,
3.3-V LVTTL functional I/O, reserved JTAG pins, 5 V VCCINT and 3.3 V VCCIO.
This does not establish physical supply levels, bus timing or safe operation.

Validation / release gate
-------------------------
Run the existing Icarus suite through Make, for example with MSYS2 UCRT64:
  make -C software\orangepi\cpld test IVERILOG=C:\msys64\ucrt64\bin\iverilog.exe VVP=C:\msys64\ucrt64\bin\vvp.exe
IVERILOG and VVP default to executable names on PATH.

run-tests.ps1 (Icarus) sweeps all 65,536 memory addresses, all 256 low I/O
ports, ROM writes, profile/slot/page filtering, BIOS startup hold, data-before-
WAIT setup, token errors, reset rearm and turnaround. Zero-delay functional
tests do NOT establish analog levels, actual GPIO speed or fitted timing.
The current suite passes 919,809 checks.
The current engine uses the MSX slot clock for capture, synchronizers and
data setup, adding several clock periods even with an instantaneous host.
Parallel transport removes SPI packet overhead, not those RTL stages. Compare
real WAIT counts; faster handshaking/clocking may be needed for the final
performance target. Do not assume a faster Pi alone eliminates these waits.
Run ../host/run-tests.ps1 for the portable C word decoder.
Install legacy Quartus II 13.0 SP1 with MAX7000S support:
  python generate_quartus.py
  quartus_sh --flow compile neo9990_bus
Verify fixed pins, JTAG/open-drain assignments, 3.3-V LVTTL model, resource use,
host-strobe/READY product-term clock legality, reset/CDC and constrained timing.
The experimental v0.1 vendor fit and POF do not establish timing closure or
full GFX9000/ROM/OPL4 compatibility. Resolve the missing SDC constraints and
critical timing warnings before treating this as a hardware release.
Program only on an isolated powered fixture, MSX disconnected, READY low,
both 5 V VCCINT and Pi-provided 3.3 V VCCIO verified. JTAG Vref is not power.
The isolated fixture must establish defined MSX input levels (RESET and
inactive strobes/address/data/clock), not leave the cartridge inputs floating.
Compare worst-case WAIT counts against PicoVerse on the same MSX/profile,
under simultaneous display/audio load, before claiming equivalent performance.
