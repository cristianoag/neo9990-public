# SPDX-License-Identifier: GPL-2.0-only
"""Execute the real Image under AArch64 instruction emulation, not a board model."""
from pathlib import Path
import struct

from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_INSN, UC_HOOK_MEM_WRITE, UC_HOOK_MEM_READ
from unicorn.arm64_const import UC_ARM64_INS_MRS, UC_ARM64_REG_PSTATE, UC_ARM64_REG_X0


IMAGE = Path(__file__).resolve().parents[1] / "build" / "Image"
BASE, FDT, FB, UART, PIO = 0x42000000, 0x4FA00000, 0x50000000, 0x05000000, 0x0300B000
PH, PI = PIO+7*0x24, PIO+8*0x24


def make_fdt(framebuffer=True, mode="demo"):
    structure, strings = bytearray(), bytearray()

    def word(value):
        structure.extend(struct.pack(">I", value))

    def align():
        while len(structure) % 4:
            structure.append(0)

    def begin(name):
        word(1)
        structure.extend(name.encode()+b"\0")
        align()

    def prop(name, value):
        word(3)
        word(len(value))
        word(len(strings))
        strings.extend(name.encode()+b"\0")
        structure.extend(value)
        align()

    def integer(name, value):
        prop(name, struct.pack(">I", value))

    begin("")
    prop("compatible", b"xunlong,orangepi-zero2w\0allwinner,sun50i-h618\0")
    integer("#address-cells", 2)
    integer("#size-cells", 2)
    begin("chosen")
    integer("#address-cells", 2)
    integer("#size-cells", 2)
    prop("neo9990,mode", mode.encode()+b"\0")
    if framebuffer:
        begin("framebuffer@50000000")
        prop("compatible", b"simple-framebuffer\0")
        prop("reg", struct.pack(">QQ", FB, 640*480*4))
        integer("width", 640)
        integer("height", 480)
        integer("stride", 640*4)
        prop("format", b"x8r8g8b8\0")
        word(2)
    word(2)
    word(2)
    word(9)
    total = 56+len(structure)+len(strings)
    header = struct.pack(">10I", 0xD00DFEED, total, 56, 56+len(structure),
                         40, 17, 16, 0, len(strings), len(structure))
    return header+bytes(16)+structure+strings


def execute(framebuffer=True, cartridge=False, el=1, valid_cpld=False):
    uc = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    uc.mem_map(BASE, 0x100000)
    uc.mem_write(BASE, IMAGE.read_bytes())
    uc.mem_map(FDT, 0x100000)
    uc.mem_write(FDT, make_fdt(framebuffer, "cartridge" if cartridge else "demo"))
    uc.mem_map(UART, 0x1000)
    uc.mem_write(UART+0x14, struct.pack("<I", 0x20))
    if framebuffer:
        uc.mem_map(FB, 0x200000)
    uc.reg_write(UC_ARM64_REG_PSTATE, 0x3C5 if el == 1 else 0x3C9)
    uc.reg_write(UC_ARM64_REG_X0, FDT)
    log = bytearray()
    counter = 0
    origin_writes = 0
    ready_high = False
    controls = 0
    direction = [0, 0]
    registers = [0x1000, 0, 0x00D4 if valid_cpld else 0, 0]
    replies = []
    ready_edges = 0
    requests = [(0x64, 0xC6), (0x63, 0x82), (0x64, 0xC8), (0x63, 0xC0)]

    def pending():
        if len(replies) < len(requests):
            address, value = requests[len(replies)]
            registers[0] = 0x9100 | value
            registers[1] = address
            registers[2] = ((len(replies)+1) << 8) | 0xD4
        else:
            # Model a reset pulse entirely missed at PH8, retained by ARMED=0.
            registers[:] = [0x1000, 0, 0x00D4, 0]

    def read(machine, access, address, size, value, _):
        if address == PI+0x10 and controls & 12 == 8:
            assert direction == [0, 0]
            machine.mem_write(address, struct.pack("<I", registers[(controls >> 4) & 3]))

    def mrs(machine, register, cp, _):
        nonlocal counter
        if (cp.op0, cp.op1, cp.crn, cp.crm) == (3, 3, 14, 0):
            if cp.op2 == 0:
                machine.reg_write(register, 24000000)
                return 1
            if cp.op2 == 1:
                counter += 24
                machine.reg_write(register, counter)
                return 1
        return 0

    def write(machine, access, address, size, value, _):
        nonlocal origin_writes, controls, ready_high, ready_edges
        if address == UART:
            log.append(value & 255)
        if address == PH+0x10:
            previous = controls
            controls = value
            assert value & (1 << 2) or value & (1 << 3), "overlapping RD/WR"
            if not value & (1 << 2):
                assert direction == [0, 0], "CPLD read while Pi drives data"
            ready_high |= bool(value & (1 << 6))
            mux = struct.unpack("<I", machine.mem_read(PH, 4))[0]
            if valid_cpld and (mux >> 12) & 15 == 1 and not previous & 8 and value & 8:
                assert direction == [0x11111111, 0x11111111], "WR capture without data outputs"
                reg = (value >> 4) & 3
                payload = struct.unpack("<I", machine.mem_read(PI+0x10, 4))[0] & 0xFFFF
                if reg == 3:
                    assert payload == 1, "native image enabled ROM or OPL4"
                    registers[3] = payload
                else:
                    assert reg == 2 and payload == (registers[2] & 0xFF00)
                    replies.append(payload)
                    pending()
            if valid_cpld and not previous & 64 and value & 64:
                assert registers[3] & 7 == 1
                registers[3] |= 0x4000
                ready_edges += 1
                if ready_edges == 2:
                    machine.emu_stop()
                else:
                    pending()
        if address in (PI, PI+4):
            direction[(address-PI)//4] = value
            if value:
                assert controls & (1 << 2), "Pi drives data while RD is asserted"
        first_pixel = FB+((480-212)//2*640+(640-256)//2)*4
        if address == first_pixel:
            origin_writes += 1
            if origin_writes == 3:
                machine.emu_stop()

    if cartridge:
        uc.mem_map(PIO, 0x1000)
        uc.mem_write(PH+0x10, struct.pack("<I", 1 << 8))
        uc.mem_write(PH, struct.pack("<I", 0x22222222))
        uc.mem_write(PH+4, struct.pack("<I", 0x55555555))
        uc.mem_write(PI+8, struct.pack("<I", 0xAAAAAAAA))
        # Undriven 0x0000 signature models a missing/unprogrammed CPLD.
    uc.hook_add(UC_HOOK_INSN, mrs, None, 1, 0, UC_ARM64_INS_MRS)
    uc.hook_add(UC_HOOK_MEM_WRITE, write)
    if cartridge:
        uc.hook_add(UC_HOOK_MEM_READ, read, begin=PI+0x10, end=PI+0x13)
    uc.emu_start(BASE, BASE+0x100000, timeout=30_000_000, count=100_000_000)
    text = log.decode("ascii")
    assert "neo9990 v0.1 native H618" in text, text
    if not framebuffer:
        assert "FATAL: FDT/display handoff" in text, text
        assert "does NOT initialize H618 HDMI" in text, text
    elif cartridge:
        if valid_cpld:
            assert "FATAL" not in text, text
            assert ready_edges == 2 and replies == [0x100, 0x200, 0x300, 0x400]
        else:
            assert "FATAL: CPLD/service" in text, text
            assert not ready_high, "unsafe missing-CPLD handling"
        assert direction == [0, 0]
        assert struct.unpack("<I", uc.mem_read(PH, 4))[0] == 0x11111122
        assert struct.unpack("<I", uc.mem_read(PH+4, 4))[0] == 0x55555500
        assert struct.unpack("<I", uc.mem_read(PI+8, 4))[0] == 0xAAAAAAA0
    else:
        assert "FATAL" not in text, text
        assert origin_writes == 3, "native image did not finish a frame"
        colors = [0xFFFFFF,0xFFFF00,0x00FFFF,0x00FF00,0xFF00FF,0xFF0000,0x0000FF,0]
        for y in range(212):
            offset = ((y+(480-212)//2)*640+(640-256)//2)*4
            pixels = struct.unpack("<256I", uc.mem_read(FB+offset, 256*4))
            assert all(pixel == 0xFF000000 | colors[x//32]
                       for x, pixel in enumerate(pixels)), (y, pixels[:4])
    label = ("missing framebuffer" if not framebuffer else
             "GPIO mailbox + missed-reset rearm" if valid_cpld else
             "missing CPLD" if cartridge else "color bars (54,272 pixels)")
    print(f"PASS: native EL{el} {label}")


if __name__ == "__main__":
    execute()
    execute(el=2)
    execute(framebuffer=False)
    execute(cartridge=True)
    execute(cartridge=True, valid_cpld=True)
