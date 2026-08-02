param(
    [string]$Model = "large"
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
    default  { "-ml" }
}

$sources = @("chess.c", "search.c", "xboard.c")
$objs = @()
foreach ($s in $sources) {
    & "$ow\binnt\wcc.exe" "-bt=dos" "-0" $modelFlag "-ox" "-d0" $s
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    $objs += [System.IO.Path]::GetFileNameWithoutExtension($s) + ".obj"
}

$linkArgs = @("system", "dos", "name", "chess.exe")
foreach ($o in $objs) { $linkArgs += "file"; $linkArgs += $o }
& "$ow\binnt\wlink.exe" @linkArgs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "Built chess.exe (16-bit, $Model model)"
