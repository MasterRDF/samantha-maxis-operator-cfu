$ErrorActionPreference='Stop'
$path=Join-Path $PSScriptRoot '.state/installation.json'
$receipt=Get-Content $path -Raw|ConvertFrom-Json
if($receipt.restored){throw 'This installation is already uninstalled'}
& (Join-Path $receipt.installer 'Deploy.ps1') -Restore
if(-not $?){throw 'Restoration stopped. Retain the release folder and backups for review.'}
$receipt.restored=$true
$receipt|ConvertTo-Json|Set-Content -LiteralPath $path -Encoding UTF8
Write-Output 'Samantha Maxis uninstalled; the preceding runtime/package was restored.'

