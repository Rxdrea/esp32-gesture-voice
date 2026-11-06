# 仅编译公开源码，不上传，不开串口，不访问云服务。
param(
 [ValidateSet('sender','receiver','all')][string]$Target='sender',
 [string]$Cli='C:\Program Files\Arduino IDE\resources\app\node_modules\arduino-ide-extension\build\arduino-cli.exe',
 [string]$ConfigFile=(Join-Path $env:USERPROFILE '.arduinoIDE\arduino-cli.yaml'),
 [string]$BuildPath='D:\Codex\build\esp32-gesture-voice-v1.2.2'
)
$ErrorActionPreference='Stop'
if (!(Test-Path -LiteralPath $Cli)) { throw '请用-Cli指定Arduino CLI位置。' }
New-Item -ItemType Directory -Path $BuildPath -Force | Out-Null
$env:TEMP=Join-Path $BuildPath 'tmp'; $env:TMP=$env:TEMP
New-Item -ItemType Directory -Path $env:TEMP -Force | Out-Null
$names=@()
if ($Target -eq 'sender' -or $Target -eq 'all') { $names+='GestureVoice' }
if ($Target -eq 'receiver' -or $Target -eq 'all') { $names+='BrainReceiver' }
foreach ($name in $names) {
 & $Cli compile --config-file $ConfigFile --fqbn 'esp32:esp32:esp32:PartitionScheme=default_8MB,FlashSize=8M,PSRAM=disabled' --build-path (Join-Path $BuildPath $name) (Join-Path $PSScriptRoot $name)
 if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
exit 0
