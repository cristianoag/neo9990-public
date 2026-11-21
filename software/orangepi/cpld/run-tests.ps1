param(
    [string]$Iverilog = "iverilog",
    [string]$Vvp = "vvp"
)
$ErrorActionPreference = "Stop"
$output = Join-Path ([System.IO.Path]::GetTempPath()) ("neo9990-" + [guid]::NewGuid() + ".vvp")
try {
    & $Iverilog -g2012 -s tb_neo9990_bus -o $output (Join-Path $PSScriptRoot "neo9990_bus.v") (Join-Path $PSScriptRoot "tb_neo9990_bus.v")
    if ($LASTEXITCODE -ne 0) { throw "Verilog compilation failed ($LASTEXITCODE)" }
    & $Vvp $output
    if ($LASTEXITCODE -ne 0) { throw "CPLD simulation failed ($LASTEXITCODE)" }
}
finally {
    if (Test-Path -LiteralPath $output) { Remove-Item -LiteralPath $output -ErrorAction Stop }
}
