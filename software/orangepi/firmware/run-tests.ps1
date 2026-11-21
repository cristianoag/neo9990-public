# SPDX-License-Identifier: GPL-2.0-only
param([string]$Cc = "gcc", [string]$Preview = "")
$ErrorActionPreference = "Stop"
$output = Join-Path ([System.IO.Path]::GetTempPath()) ("neo9990-firmware-" + [guid]::NewGuid() + ".exe")
$previousPath = $env:PATH
try {
    $compiler = (Get-Command $Cc -ErrorAction Stop).Source
    $env:PATH = (Split-Path -Parent $compiler) + ";" + $env:PATH
    $sources = @("src\v9990.c", "src\fdt.c", "src\service.c", "tests\test_firmware.c") |
        ForEach-Object { Join-Path $PSScriptRoot $_ }
    & $compiler -std=c11 -O2 -g -Wall -Wextra -Werror -pedantic -I (Join-Path $PSScriptRoot "include") @sources -o $output
    if ($LASTEXITCODE -ne 0) { throw "Firmware test compilation failed ($LASTEXITCODE)" }
    if ($Preview) { & $output $Preview } else { & $output }
    if ($LASTEXITCODE -ne 0) { throw "Firmware tests failed ($LASTEXITCODE)" }
}
finally {
    $env:PATH = $previousPath
    if (Test-Path -LiteralPath $output) { Remove-Item -LiteralPath $output -ErrorAction Stop }
}
