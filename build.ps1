param(
    [string]$Model = "large",
    [switch]$Profile,    # -Profile: build with -DPROFILE so `chess profile` works
    [switch]$NoNNUE,     # -NoNNUE: build with -DNO_NNUE (material eval, no net load)
    [switch]$MameMarkers, # -MameMarkers: emit port-E9 calibration markers
    [switch]$TimingDetail, # -TimingDetail: print unrounded emulator timing totals
    [switch]$Tune,        # -Tune: expose search parameters for SPSA validation
    [switch]$NoAsmAttack,  # -NoAsmAttack: compile the C is_attacked instead of
                           # attack_opt.asm (kept to reproduce pre-adoption numbers)
    [switch]$NoAsmGenCaps, # -NoAsmGenCaps: compile the C gen_caps instead of gen_opt.asm
    [switch]$AtkDiff,      # -AtkDiff: also link the C reference and `chess atkdiff`
                           # (test scaffolding only)
    [switch]$GenCapsDiff,   # -GenCapsDiff: also link the C reference and `chess gencaps`
                           # (test scaffolding only)
    [switch]$NNTest,        # -NNTest: compile the NNUE selftest (`chess nn`)
                            # (test scaffolding only)
    [switch]$DebugLog,      # -DebugLog: write the protocol log (chess_debug_*.txt)
                            # (diagnostic only)
    [switch]$PerftTest      # -PerftTest: compile tests/perft_test.inc and `chess perft`
                           # (test scaffolding only)
)

# The two hand-written ports are the default 16-bit hot paths: measured on the
# 80186 at -56.5% for gen_caps and -4.5% for is_attacked, with an identical
# bench tree (see artifacts/gencaps-asm-2026-11-04 and artifacts/attack-asm-2026-11-04).
$asmAttack  = -not $NoAsmAttack
$asmGenCaps = -not $NoAsmGenCaps
if ($AtkDiff)     { $asmAttack  = $true }
if ($GenCapsDiff) { $asmGenCaps = $true }

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

$sources = @("chess.c", "search.c", "xboard.c", "nnue.c", "vclock.c", "timing.c", "tt.c")
if ($AtkDiff) { $sources += "attack_ref.c" }
if ($GenCapsDiff) { $sources += "gen_ref.c" }
$objs = @()
foreach ($s in $sources) {
    # -ox enables the full optimizer set but leaves the size/speed trade-off at
    # the compiler default; -os (favor code size) re-blocks the hot C paths to
    # fall through instead of jumping, which measured 3.6-5.7% faster on
    # 8088/80186/80286 (identical bench tree). See vclock.c for the model rescale.
    $flags = @("-bt=dos", "-0", $modelFlag, "-ox", "-os", "-d0")
    if ($Profile) { $flags += "-DPROFILE" }
    if ($NoNNUE)  { $flags += "-DNO_NNUE" }
    if ($MameMarkers) { $flags += "-DMAME_MARKERS" }
    if ($TimingDetail) { $flags += "-DTIMING_DETAIL" }
    if ($Tune) { $flags += "-DTUNE" }
    if ($asmAttack) { $flags += "-DASM_ISATTACKED" }
    if ($AtkDiff) { $flags += "-DATK_DIFF" }
    if ($asmGenCaps) { $flags += "-DASM_GENCAPS" }
    if ($GenCapsDiff) { $flags += "-DGENCAPS_DIFF" }
    if ($PerftTest) { $flags += "-DPERFT_TEST" }
    if ($NNTest)   { $flags += "-DNN_TEST" }
    if ($DebugLog) { $flags += "-DDBG_LOG" }
    & "$ow\binnt\wcc.exe" @flags $s
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    $objs += [System.IO.Path]::GetFileNameWithoutExtension($s) + ".obj"
}

# hand-unrolled NNUE apply loops (only used by the 16-bit build)
& "$ow\binnt\wasm.exe" "nnue_opt.asm"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$objs += "nnue_opt.obj"

if ($asmAttack) {
    # hand-written is_attacked scan (see attack_opt.asm)
    & "$ow\binnt\wasm.exe" "attack_opt.asm"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    $objs += "attack_opt.obj"
}

if ($asmGenCaps) {
    # hand-written capture generator (see gen_opt.asm)
    & "$ow\binnt\wasm.exe" "gen_opt.asm"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    $objs += "gen_opt.obj"
}

$linkArgs = @("system", "dos", "name", "chess.exe")
foreach ($o in $objs) { $linkArgs += "file"; $linkArgs += $o }
# Search can reach MAXPLY=32; its recursive frames and helper calls exceed
# the linker's 2 KB default. Reserve 8 KB, including saved Undo signatures.
& "$ow\binnt\wlink.exe" @linkArgs option stack=8192 option map
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if (-not $NoNNUE) {
    # DOS loads the official net through its 8.3 filename.
    Copy-Item -LiteralPath (Join-Path $root "short-net2-finetune2.net") `
              -Destination (Join-Path $root "chess.net") -Force
}

Write-Host "Built chess.exe (16-bit, $Model model)"
