param(
    [string]$Model = "large",
    [string]$CmdArgs = "",
    [string]$Cycles = "max",
    [string]$Fen = "",
    [switch]$Profile,  # -Profile: build with -DPROFILE so `chess profile` works
    [switch]$NoAsmAttack,  # -NoAsmAttack: C is_attacked (pre-adoption baseline)
    [switch]$NoAsmGenCaps, # -NoAsmGenCaps: C gen_caps (pre-adoption baseline)
    [switch]$PerftTest,    # -PerftTest: include the perft test CLI
    [switch]$NNTest,       # -NNTest: include the NNUE selftest (`chess nn`)
    [switch]$DebugLog      # -DebugLog: write the protocol debug log
)

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$ow   = "C:\ow"
$dosbox = "C:\Users\ckbao\AppData\Local\Programs\DOSBox Staging\dosbox.exe"

# 1. Build
pushd $root
$buildArgs = @("-Model", $Model)
if ($Profile)       { $buildArgs += "-Profile" }
if ($NoAsmAttack)   { $buildArgs += "-NoAsmAttack" }
if ($NoAsmGenCaps)  { $buildArgs += "-NoAsmGenCaps" }
if ($PerftTest)     { $buildArgs += "-PerftTest" }
if ($NNTest)        { $buildArgs += "-NNTest" }
if ($DebugLog)      { $buildArgs += "-DebugLog" }
& powershell -NoProfile -ExecutionPolicy Bypass -File "$root\build.ps1" @buildArgs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
popd

# 2. Run in DOSBox and capture stdout to result.txt
$exeName = "chess.exe"
if ($Fen -ne "") { Set-Content -Path "$root\fen.txt" -Value $Fen -NoNewline }
$runLine = $exeName + " " + $CmdArgs + " > result.txt"
Remove-Item "$root\result.txt" -ErrorAction SilentlyContinue
& $dosbox --exit --set cpu_cycles=$Cycles -c "mount c $root" -c "c:" -c $runLine *> "$root\dosbox.log"
Get-Content "$root\result.txt" -ErrorAction SilentlyContinue
