VERSION    ?= v0.1
PYTHON     ?= python
QUARTUS_SH ?= quartus_sh
POWERSHELL ?= powershell
IVERILOG   ?= iverilog
VVP        ?= vvp

.PHONY: all generate build test clean

all: build
	"$(PYTHON)" -c "from pathlib import Path; import shutil; src = Path('output_files') / 'neo9990_bus.pof'; dst = Path.cwd().parents[2] / 'firmware' / 'neo9990-orangepi-cpld-$(VERSION).pof'; dst.parent.mkdir(exist_ok=True); shutil.copy2(src, dst); print('Built ' + str(dst))"

generate:
	"$(PYTHON)" generate_quartus.py

build: generate
	"$(QUARTUS_SH)" --flow compile neo9990_bus

test:
	"$(POWERSHELL)" -NoProfile -ExecutionPolicy Bypass -File run-tests.ps1 -Iverilog "$(IVERILOG)" -Vvp "$(VVP)"

clean:
	"$(PYTHON)" -c "from pathlib import Path; import shutil; [shutil.rmtree(name) for name in ('db', 'incremental_db', 'output_files') if Path(name).is_dir()]"
