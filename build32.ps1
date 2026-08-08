param(
    [switch]$Profile   # -Profile: build with -DPROFILE so `chess profile` works
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$ow   = "C:\ow"

$env:WATCOM  = $ow
$env:PATH    = "$ow\binnt;" + $env:PATH
$env:INCLUDE = "$ow\h"
$env:LIB     = "$ow\lib386\nt"

$sources = @("chess.c", "search.c", "xboard.c", "nnue.c", "vclock.c", "tt.c")
$objs = @()
foreach ($s in $sources) {
    # -3 = 386 target (oldest 32-bit CPU): simplest, most sequential codegen, closest
    # in character to the 16-bit build's -0 (8086) code. Keeps -ox parity so the
    # relative cost profile tracks the 16-bit build (see NOTES.md "Speed fidelity").
    $flags = @("-bt=nt", "-3", "-ox", "-d0")
    if ($Profile) { $flags += "-DPROFILE" }
    & "$ow\binnt\wcc386.exe" @flags $s
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    $objs += [System.IO.Path]::GetFileNameWithoutExtension($s) + ".obj"
}

$linkArgs = @("system", "nt", "name", "chess32.exe")
foreach ($o in $objs) { $linkArgs += "file"; $linkArgs += $o }
& "$ow\binnt\wlink.exe" @linkArgs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "Built chess32.exe (32-bit Win32 console, same code as 16-bit build)"
