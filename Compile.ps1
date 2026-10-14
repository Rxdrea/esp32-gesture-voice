# 只编译，不上传、不打开串口。
param([string]$Cli = 'C:\Program Files\Arduino IDE\resources\app\node_modules\arduino-ide-extension\build\arduino-cli.exe', [string]$BuildPath = 'C:\temp\esp32-gesture-voice-public-build')
$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $Cli)) { throw 'Arduino CLI not found; set -Cli or use Arduino IDE.' }
$config = Join-Path $env:USERPROFILE '.arduinoIDE\arduino-cli.yaml'
& $Cli compile --config-file $config --fqbn 'esp32:esp32:esp32:PartitionScheme=default_8MB,FlashSize=8M,PSRAM=disabled' --build-path $BuildPath (Join-Path $PSScriptRoot 'GestureVoice')
exit $LASTEXITCODE
