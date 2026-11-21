param([string]$Cc = "gcc")
$ErrorActionPreference = "Stop"
$output = Join-Path ([System.IO.Path]::GetTempPath()) ("neo9990-host-" + [guid]::NewGuid() + ".exe")
$previousPath = $env:PATH
try {
    $compiler = (Get-Command $Cc -ErrorAction Stop).Source
    $env:PATH = (Split-Path -Parent $compiler) + ";" + $env:PATH
    & $compiler -std=c11 -Wall -Wextra -Werror -pedantic (Join-Path $PSScriptRoot "test_parallel_protocol.c") -o $output
    if ($LASTEXITCODE -ne 0) { throw "Host protocol compilation failed ($LASTEXITCODE)" }
    & $output
    if ($LASTEXITCODE -ne 0) { throw "Host protocol tests failed ($LASTEXITCODE)" }
}
finally {
    $env:PATH = $previousPath
    if (Test-Path -LiteralPath $output) { Remove-Item -LiteralPath $output -ErrorAction Stop }
}
