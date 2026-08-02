param(
    [string]$Source = "chess.c"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$ow   = "C:\ow"

$env:WATCOM  = $ow
$env:PATH    = "$ow\binnt;" + $env:PATH
$env:INCLUDE = "$ow\h"
$env:LIB     = "$ow\lib386\nt"

$srcName = [System.IO.Path]::GetFileNameWithoutExtension($Source)
$exeName = $srcName + "32.exe"

& "$ow\binnt\wcc386.exe" "-bt=nt" "-ox" "-d0" $Source
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& "$ow\binnt\wlink.exe" system nt name $exeName file ($srcName + ".obj")
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "Built $exeName (32-bit Win32 console, same code as 16-bit build)"
