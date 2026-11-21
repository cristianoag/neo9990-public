# SPDX-License-Identifier: GPL-2.0-only
"""Build the freestanding AArch64 image; never access or flash a device."""
import argparse
import os
from pathlib import Path
import shutil
import struct
import subprocess


ROOT = Path(__file__).resolve().parent


def tool(name):
    result = shutil.which(name)
    if result is None:
        raise SystemExit(f"Required build tool not found: {name}. Install LLVM clang and lld.")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="clang")
    parser.add_argument("--ld", default="ld.lld")
    args = parser.parse_args()
    cc, ld = tool(args.cc), tool(args.ld)
    env = os.environ.copy()
    env["PATH"] = str(Path(cc).parent) + os.pathsep + env["PATH"]
    output = ROOT / "build"
    output.mkdir(exist_ok=True)
    sources = ("src/v9990.c", "src/fdt.c", "src/service.c", "platform/h618.c",
               "platform/kernel.c", "platform/runtime.c", "platform/start.S")
    flags = ["--target=aarch64-none-elf", "-mcpu=cortex-a53", "-mgeneral-regs-only",
             "-ffreestanding", "-fno-builtin", "-fno-stack-protector", "-fno-pic",
             "-fno-unwind-tables", "-fno-asynchronous-unwind-tables", "-mstrict-align",
             "-O2", "-g", "-Wall", "-Wextra", "-Werror",
             "-I", str(ROOT / "include"), "-I", str(ROOT / "platform")]
    objects = []
    for name in sources:
        obj = output / (Path(name).stem + ".o")
        subprocess.run([cc, *flags, "-c", str(ROOT / name), "-o", str(obj)],
                       check=True, env=env)
        objects.append(str(obj))
    common = ["-m", "aarch64elf", "-T", str(ROOT / "platform" / "linker.ld"), *objects]
    subprocess.run([ld, *common, "-Map=" + str(output / "neo9990.map"),
                    "-o", str(output / "neo9990.elf")], check=True, env=env)
    subprocess.run([ld, *common, "--oformat", "binary", "-o", str(output / "Image")],
                   check=True, env=env)
    image = (output / "Image").read_bytes()
    elf = (output / "neo9990.elf").read_bytes()
    offset, size, flags_value = struct.unpack_from("<QQQ", image, 8)
    assert image[56:60] == b"ARM\x64", "Missing AArch64 Linux boot-protocol header"
    assert offset == 0x2000000 and len(image) <= size < 0x1000000 and flags_value == 0
    assert elf[:6] == b"\x7fELF\x02\x01" and struct.unpack_from("<H", elf, 18)[0] == 183
    assert struct.unpack_from("<Q", elf, 24)[0] == 0x42000000
    print(f"Built and checked AArch64 Image: {len(image)} file bytes, {size} RAM bytes.")
    print("Bootloader handoff image only: no SPL, DRAM initialization or H618 HDMI driver.")
    print("Nothing has been flashed or hardware-tested.")


if __name__ == "__main__":
    main()
