param([string]$GamePath)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
if(-not $GamePath){$GamePath=Read-Host 'Full path to the cod25 folder'}
$game=(Resolve-Path -LiteralPath $GamePath).Path
if(-not (Test-Path -LiteralPath (Join-Path $game 'cod25-cod.exe') -PathType Leaf)){throw 'Choose the cod25 folder containing cod25-cod.exe'}
if(@(Get-Process -Name 'cod25-cod','cod25','KronosLauncher' -ErrorAction SilentlyContinue).Count){throw 'Close BO7 and all Kronos windows first'}
$receipt=Join-Path $PSScriptRoot '.state/installation.json'
if(Test-Path -LiteralPath $receipt){$old=Get-Content $receipt -Raw|ConvertFrom-Json;if(-not $old.restored){throw 'An installation is already recorded. Run Uninstall.cmd from this folder before reinstalling.'}}
$checks=Get-Content (Join-Path $PSScriptRoot 'release-files.json') -Raw|ConvertFrom-Json
foreach($file in $checks.files){
    $path=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot $file.path))
    $base=[IO.Path]::GetFullPath($PSScriptRoot).TrimEnd('\')+'\'
    if(-not $path.StartsWith($base,[StringComparison]::OrdinalIgnoreCase)){throw 'Release path escapes package folder'}
    if((Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant()-ne $file.sha256){throw "Release file changed: $($file.path)"}
}
$destination=Join-Path $PSScriptRoot ('.state/install-'+[guid]::NewGuid().ToString('N'))
$cli=Join-Path $PSScriptRoot 'tools/linker/bo7-linker.exe'
& $cli prepare-test --manifest (Join-Path $PSScriptRoot 'package/mod.manifest.json') --game $game --out $destination --runtime (Join-Path $PSScriptRoot 'runtime') --loader launcher --json
if($LASTEXITCODE-ne 0){throw 'Installer preparation refused the target. No installation was applied.'}
& (Join-Path $destination 'Deploy.ps1')
if(-not $?){throw "Installation failed. Retain the prepared folder for review: $destination"}
@{schemaVersion=1;gameRoot=$game;installer=$destination;restored=$false}|ConvertTo-Json|Set-Content -LiteralPath $receipt -Encoding UTF8
Write-Output 'Samantha Maxis v0.1 installed. Launch BO7 yourself, run Attach.cmd, then select Karma > Tacit in local Zombies.'

