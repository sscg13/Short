param(
    [string]$Model = "small",
    [string]$Source = "hello.c"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$ow   = "C:\ow"

$env:WATCOM = $ow
$env:PATH   = "$ow\binnt;" + $env:PATH
$env:INCLUDE = "$ow\h"
$env:LIB     = "$ow\lib286"

$modelFlag = switch ($Model) {
    "small"  { "-ms" }
    "medium" { "-mm" }
    "large"  { "-ml" }
    "huge"   { "-mh" }
    default  { "-ms" }
}

$srcName  = [System.IO.Path]::GetFileNameWithoutExtension($Source)
$exeName  = $srcName + ".exe"

$args = @(
    "-bt=dos",   # DOS target
    "-0",        # 8086/8088 instruction set
    $modelFlag,
    "-ox",       # optimize for speed
    "-d0",       # no debug info
    $Source
)
& "$ow\binnt\wcc.exe" @args
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& "$ow\binnt\wlink.exe" system dos name $exeName file ($srcName + ".obj")
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "Built $exeName (16-bit, $Model model)"
