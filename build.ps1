param(
    [string]$Model = "large",
    [switch]$Profile,   # -Profile: build with -DPROFILE so `chess profile` works
    [switch]$NoNNUE     # -NoNNUE: build with -DNO_NNUE (material eval, no net load)
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

$sources = @("chess.c", "search.c", "xboard.c", "nnue.c", "vclock.c", "tt.c")
$objs = @()
foreach ($s in $sources) {
    $flags = @("-bt=dos", "-0", $modelFlag, "-ox", "-d0")
    if ($Profile) { $flags += "-DPROFILE" }
    if ($NoNNUE)  { $flags += "-DNO_NNUE" }
    & "$ow\binnt\wcc.exe" @flags $s
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    $objs += [System.IO.Path]::GetFileNameWithoutExtension($s) + ".obj"
}

# hand-unrolled NNUE apply loops (only used by the 16-bit build)
& "$ow\binnt\wasm.exe" "nnue_opt.asm"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$objs += "nnue_opt.obj"

$linkArgs = @("system", "dos", "name", "chess.exe")
foreach ($o in $objs) { $linkArgs += "file"; $linkArgs += $o }
& "$ow\binnt\wlink.exe" @linkArgs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "Built chess.exe (16-bit, $Model model)"
