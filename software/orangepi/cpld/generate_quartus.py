"""Generate legacy Quartus assignments from the same pin map used by the PCB."""

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parent


def main():
    pins = json.loads((ROOT / "pins.json").read_text())
    lines = [
        'set_global_assignment -name FAMILY MAX7000S',
        f'set_global_assignment -name DEVICE {pins["device"]}',
        'set_global_assignment -name TOP_LEVEL_ENTITY neo9990_bus',
        'set_global_assignment -name ORIGINAL_QUARTUS_VERSION "13.0 SP1"',
        'set_global_assignment -name PROJECT_OUTPUT_DIRECTORY output_files',
        'set_global_assignment -name VERILOG_FILE neo9990_bus.v',
        'set_global_assignment -name VERILOG_INPUT_VERSION VERILOG_2001',
        'set_global_assignment -name MAX7000_DEVICE_IO_STANDARD "3.3-V LVTTL"',
        'set_global_assignment -name ENABLE_JTAG_BST_SUPPORT ON',
        'set_global_assignment -name RESERVE_ALL_UNUSED_PINS_NO_OUTPUT_GND "AS INPUT TRI-STATED"',
        'set_global_assignment -name AUTO_TURBO_BIT ON',
        'set_global_assignment -name SLOW_SLEW_RATE OFF',
    ]
    for port, data in pins["ports"].items():
        lines.append(f'set_location_assignment PIN_{data["pin"]} -to "{port}"')
        if data["direction"] == "open_collector":
            lines.append(f'set_instance_assignment -name AUTO_OPEN_DRAIN ON -to "{port}"')
    (ROOT / "neo9990_bus.qsf").write_text("\n".join(lines)+"\n")
    (ROOT / "neo9990_bus.qpf").write_text(
        'QUARTUS_VERSION = "13.0"\nPROJECT_REVISION = "neo9990_bus"\n')
    print(f'Generated {pins["device"]} locations; 3.3 V I/O, reserved JTAG, open-drain MSX controls.')


if __name__ == "__main__":
    main()
