$ErrorActionPreference='Stop'
$receipt=Get-Content (Join-Path $PSScriptRoot '.state/installation.json') -Raw|ConvertFrom-Json
if($receipt.restored){throw 'This installation was uninstalled'}
& (Join-Path $PSScriptRoot 'tools/linker/bo7-linker.exe') attach --game $receipt.gameRoot
if($LASTEXITCODE-ne 0){throw 'Attach failed. If BO7 is elevated, run Attach.cmd as administrator and approve the Windows prompt yourself.'}

