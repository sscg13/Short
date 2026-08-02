$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$ow   = "C:\ow"

$env:WATCOM  = $ow
$env:PATH    = "$ow\binnt;" + $env:PATH
$env:INCLUDE = "$ow\h"
$env:LIB     = "$ow\lib386\nt"

$sources = @("chess.c", "search.c", "xboard.c")
$objs = @()
foreach ($s in $sources) {
    & "$ow\binnt\wcc386.exe" "-bt=nt" "-ox" "-d0" $s
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    $objs += [System.IO.Path]::GetFileNameWithoutExtension($s) + ".obj"
}

$linkArgs = @("system", "nt", "name", "chess32.exe")
foreach ($o in $objs) { $linkArgs += "file"; $linkArgs += $o }
& "$ow\binnt\wlink.exe" @linkArgs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "Built chess32.exe (32-bit Win32 console, same code as 16-bit build)"
