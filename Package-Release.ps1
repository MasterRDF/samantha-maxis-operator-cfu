param([Parameter(Mandatory=$true)][string]$FastfileFolder,[Parameter(Mandatory=$true)][string]$RuntimeFolder,[Parameter(Mandatory=$true)][string]$LinkerFolder)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$destination=Join-Path $PSScriptRoot 'dist/Samantha-Maxis-Operator-CFU-v0.1'
if(Test-Path -LiteralPath $destination){throw 'Release output already exists; retain it and choose a fresh checkout/output for rebuilding'}
$manifest=Get-Content (Join-Path $PSScriptRoot 'package/mod.manifest.json') -Raw|ConvertFrom-Json
foreach($file in $manifest.files){
    $path=Join-Path $FastfileFolder $file.name
    if((Get-Item -LiteralPath $path).Length-ne $file.bytes-or (Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant()-ne $file.sha256){throw "Fastfile input changed: $($file.name)"}
}
New-Item -ItemType Directory -Path (Join-Path $destination 'package'),(Join-Path $destination 'runtime'),(Join-Path $destination 'tools/linker') -Force|Out-Null
foreach($file in $manifest.files){Copy-Item -LiteralPath (Join-Path $FastfileFolder $file.name) -Destination (Join-Path $destination 'package')}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'package/mod.manifest.json') -Destination (Join-Path $destination 'package')
foreach($name in @('bo7-runtime.dll','runtime.json','Deploy.ps1','XINPUT9_1_0.dll')){Copy-Item -LiteralPath (Join-Path $RuntimeFolder $name) -Destination (Join-Path $destination 'runtime')}
foreach($name in @('bo7-linker.exe','bo7-linker.dll','bo7-linker.deps.json','bo7-linker.runtimeconfig.json','Apothicon.Linker.Core.dll')){Copy-Item -LiteralPath (Join-Path $LinkerFolder $name) -Destination (Join-Path $destination 'tools/linker')}
foreach($name in @('README.md','COMPATIBILITY.md','CHANGELOG.md','THIRD-PARTY-NOTICES.md','Install.ps1','Attach.ps1','Uninstall.ps1','Install.cmd','Attach.cmd','Uninstall.cmd')){Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination $destination}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'licenses') -Destination (Join-Path $destination 'licenses') -Recurse
$files=@(Get-ChildItem -LiteralPath $destination -Recurse -File|ForEach-Object {@{path=$_.FullName.Substring($destination.Length+1).Replace('\','/');bytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName).Hash.ToLowerInvariant()}})
@{schemaVersion=1;version='0.1';files=$files}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $destination 'release-files.json') -Encoding UTF8
$archive=$destination+'.zip'
Compress-Archive -LiteralPath $destination -DestinationPath $archive -CompressionLevel Optimal
Get-FileHash -LiteralPath $archive -Algorithm SHA256
