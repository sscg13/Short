param(
    [string]$Model = "small",
    [string]$Source = "hello.c",
    [string]$CmdArgs = "",
    [string]$Cycles = "max",
    [string]$Fen = ""
)

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$ow   = "C:\ow"
$dosbox = "C:\Users\ckbao\AppData\Local\Programs\DOSBox Staging\dosbox.exe"

# 1. Build
pushd $root
& powershell -NoProfile -ExecutionPolicy Bypass -File "$root\build.ps1" -Model $Model -Source $Source
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
popd

# 2. Run in DOSBox and capture stdout to result.txt
$exeName = [System.IO.Path]::GetFileNameWithoutExtension($Source) + ".exe"
if ($Fen -ne "") { Set-Content -Path "$root\fen.txt" -Value $Fen -NoNewline }
$runLine = $exeName + " " + $CmdArgs + " > result.txt"
Remove-Item "$root\result.txt" -ErrorAction SilentlyContinue
& $dosbox --exit --set cpu_cycles=$Cycles -c "mount c $root" -c "c:" -c $runLine *> "$root\dosbox.log"
Get-Content "$root\result.txt" -ErrorAction SilentlyContinue
