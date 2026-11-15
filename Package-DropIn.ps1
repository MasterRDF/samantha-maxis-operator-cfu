param([Parameter(Mandatory=$true)][string]$FastfileFolder,[string]$NativeBuildFolder=(Join-Path $PSScriptRoot 'build/dropin'))
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$destination=Join-Path $PSScriptRoot 'dist/Samantha-Maxis-Operator-CFU-v0.1-Drag-and-Drop'
if(Test-Path -LiteralPath $destination){throw 'Output already exists; retain the previous build and use a fresh output checkout'}
$manifestPath=Join-Path $PSScriptRoot 'package/dropin/mod.manifest.json'
$manifest=Get-Content $manifestPath -Raw|ConvertFrom-Json
$selection=Get-Content (Join-Path $PSScriptRoot 'package/dropin/dropin.json') -Raw|ConvertFrom-Json
if($selection.manifestSha256-ne (Get-FileHash $manifestPath).Hash.ToLowerInvariant()){throw 'Drop-in selection does not match its manifest'}
foreach($file in $manifest.files){
    $path=Join-Path $FastfileFolder $file.name
    if((Get-Item -LiteralPath $path).Length-ne $file.bytes-or (Get-FileHash $path).Hash.ToLowerInvariant()-ne $file.sha256){throw "Fastfile changed: $($file.name)"}
}
$native=Join-Path $destination 'cod25/kronos/mods/samantha_maxis_operator_cfu/native'
New-Item -ItemType Directory -Path $native -Force|Out-Null
Copy-Item -LiteralPath (Join-Path $NativeBuildFolder 'tbs.dll') -Destination (Join-Path $destination 'cod25/tbs.dll')
Copy-Item -LiteralPath (Join-Path $NativeBuildFolder 'maxis-runtime.dll') -Destination $native
foreach($name in @('mod.manifest.json','dropin.json')){Copy-Item -LiteralPath (Join-Path $PSScriptRoot ('package/dropin/'+$name)) -Destination $native}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'package/dropin/mod.json') -Destination (Split-Path -Parent $native)
foreach($file in $manifest.files){Copy-Item -LiteralPath (Join-Path $FastfileFolder $file.name) -Destination (Join-Path $destination 'cod25')}
foreach($name in @('README.md','COMPATIBILITY.md','CHANGELOG.md','BUILD.md','RELEASE_NOTES.md','THIRD-PARTY-NOTICES.md')){Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination $destination}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'licenses') -Destination (Join-Path $destination 'licenses') -Recurse
$files=@(Get-ChildItem -LiteralPath (Join-Path $destination 'cod25') -Recurse -File|ForEach-Object {@{path=$_.FullName.Substring($destination.Length+1).Replace('\','/');bytes=$_.Length;sha256=(Get-FileHash $_.FullName).Hash.ToLowerInvariant()}})
@{schemaVersion=1;version='0.1';format='drag-and-drop';files=$files}|ConvertTo-Json -Depth 5|Set-Content (Join-Path $destination 'package-files.json') -Encoding UTF8
$archive=$destination+'.zip'
Compress-Archive -LiteralPath $destination -DestinationPath $archive -CompressionLevel Optimal
Get-FileHash -LiteralPath $archive -Algorithm SHA256
