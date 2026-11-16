param([Parameter(Mandatory=$true)][string]$KronosDll,[Parameter(Mandatory=$true)][string]$OperatorsManifest,[Parameter(Mandatory=$true)][string]$ReleasedOperatorsDll)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$repo=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$source=Join-Path $repo 'src/runtime'
$build=Join-Path $repo 'build/cfu-tests'
New-Item -ItemType Directory -Path $build -Force|Out-Null
$finder=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$installation=& $finder -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$vcvars=Join-Path $installation 'VC/Auxiliary/Build/vcvars64.bat'
cmd /d /c "`"$vcvars`" >nul && set"|ForEach-Object {if($_ -match '^([^=]+)=(.*)$'){[Environment]::SetEnvironmentVariable($Matches[1],$Matches[2],'Process')}}
if($LASTEXITCODE-ne 0){throw 'MSVC environment failed'}
& cl.exe /nologo /std:c++20 /W4 /WX /EHsc /MT /O2 /DNOMINMAX /DWIN32_LEAN_AND_MEAN "/I$source" "/I$PSScriptRoot/cfu-reference" "/Fe:$build/cfu-profile-tests.exe" "/Fo:$build/" (Join-Path $PSScriptRoot 'CfuProfileFixture.cpp') (Join-Path $PSScriptRoot 'cfu-reference/ops_patch.cpp') /link bcrypt.lib
if($LASTEXITCODE-ne 0){throw 'CFU fixture compile failed'}
& (Join-Path $build 'cfu-profile-tests.exe') $KronosDll $OperatorsManifest $ReleasedOperatorsDll
if($LASTEXITCODE-ne 0){throw 'CFU fixture failed'}
& cl.exe /nologo /std:c++20 /W4 /WX /EHsc /MT /O2 "/I$source" "/Fe:$build/shared-slot-tests.exe" "/Fo:$build/shared-slot.obj" (Join-Path $PSScriptRoot 'shared-slot-test.cpp') /link bcrypt.lib advapi32.lib psapi.lib
if($LASTEXITCODE-ne 0){throw 'Sharing fixture compile failed'}
& (Join-Path $build 'shared-slot-tests.exe')
if($LASTEXITCODE-ne 0){throw 'Sharing fixture failed'}
