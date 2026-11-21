# SPDX-License-Identifier: GPL-2.0-only

VERSION    ?= v0.1
PYTHON     ?= python
CLANG      ?= clang
LD_LLD     ?= ld.lld
HOST_CC    ?= gcc
POWERSHELL ?= powershell

.PHONY: all build test test-native clean

all: build
	"$(PYTHON)" -c "from pathlib import Path; import shutil; src = Path('build') / 'Image'; dst = Path.cwd().parents[2] / 'firmware' / 'neo9990-orangepi-$(VERSION).bin'; dst.parent.mkdir(exist_ok=True); shutil.copy2(src, dst); print('Built ' + str(dst))"

build:
	"$(PYTHON)" build.py --cc "$(CLANG)" --ld "$(LD_LLD)"

test:
	"$(POWERSHELL)" -NoProfile -ExecutionPolicy Bypass -File run-tests.ps1 -Cc "$(HOST_CC)"
	"$(POWERSHELL)" -NoProfile -ExecutionPolicy Bypass -File "..\host\run-tests.ps1" -Cc "$(HOST_CC)"

test-native: build
	"$(PYTHON)" "tests\test_native.py"

clean:
	"$(PYTHON)" -c "from pathlib import Path; import shutil; shutil.rmtree('build') if Path('build').is_dir() else None"
