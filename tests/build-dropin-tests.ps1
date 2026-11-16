param([Parameter(Mandatory=$true)][string]$ContractGame,[Parameter(Mandatory=$true)][string]$Scratch)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$repo=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$source=Join-Path $repo 'src/runtime'
$build=Join-Path $repo 'build/dropin-tests'
New-Item -ItemType Directory -Path $build -Force|Out-Null
$finder=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$installation=& $finder -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$vcvars=Join-Path $installation 'VC/Auxiliary/Build/vcvars64.bat'
cmd /d /c "`"$vcvars`" >nul && set"|ForEach-Object {if($_ -match '^([^=]+)=(.*)$'){[Environment]::SetEnvironmentVariable($Matches[1],$Matches[2],'Process')}}
if($LASTEXITCODE-ne 0){throw 'MSVC environment failed'}
& cl.exe /nologo /std:c++20 /W4 /WX /EHsc /MT /O2 /DBO7_RELEASE /DBO7_DROPIN "/I$source" "/Fe:$build/contract-test.exe" "/Fo:$build/contract-test.obj" (Join-Path $PSScriptRoot 'dropin-contract-test.cpp') /link bcrypt.lib advapi32.lib psapi.lib
if($LASTEXITCODE-ne 0){throw 'Contract fixture compile failed'}
& (Join-Path $build 'contract-test.exe') $ContractGame
if($LASTEXITCODE-ne 0){throw 'Contract fixture failed'}
& cl.exe /nologo /std:c++20 /W4 /WX /EHsc /MT /O2 "/I$source" "/Fe:$build/language-test.exe" "/Fo:$build/language-test.obj" (Join-Path $PSScriptRoot 'language-selection-test.cpp')
if($LASTEXITCODE-ne 0){throw 'Language fixture compile failed'}
& (Join-Path $build 'language-test.exe') ($Scratch+'-language')
if($LASTEXITCODE-ne 0){throw 'Language fixture failed'}
& cl.exe /nologo /std:c++20 /W4 /WX /EHsc /MT /O2 /LD "/Fe:$build/XINPUT9_1_0.dll" "/Fo:$build/kronos.obj" (Join-Path $PSScriptRoot 'dropin-test-kronos.cpp')
if($LASTEXITCODE-ne 0){throw 'Kronos stand-in compile failed'}
& cl.exe /nologo /std:c++20 /W4 /WX /EHsc /MT /O2 /LD "/Fe:$build/maxis-runtime.dll" "/Fo:$build/runtime.obj" (Join-Path $PSScriptRoot 'dropin-test-runtime.cpp')
if($LASTEXITCODE-ne 0){throw 'Runtime stand-in compile failed'}
$sha=(Get-FileHash (Join-Path $build 'maxis-runtime.dll')).Hash.ToLowerInvariant()
[IO.File]::WriteAllText((Join-Path $build 'dropin-runtime-pin.hpp'),('#pragma once'+[Environment]::NewLine+'inline constexpr char MaxisRuntimeSha256[]="'+$sha+'";'+[Environment]::NewLine),[Text.UTF8Encoding]::new($false))
& cl.exe /nologo /std:c++20 /W4 /WX /EHsc /MT /O2 /LD "/I$source" "/I$build" "/Fe:$build/tbs.dll" "/Fo:$build/loader.obj" (Join-Path $repo 'src/dropin/loader.cpp') /link bcrypt.lib
if($LASTEXITCODE-ne 0){throw 'Loader fixture compile failed'}
& cl.exe /nologo /std:c++20 /W4 /WX /EHsc /MT /O2 "/Fe:$build/cod25-cod.exe" "/Fo:$build/game.obj" (Join-Path $PSScriptRoot 'dropin-test-game.cpp') /link (Join-Path $build 'XINPUT9_1_0.lib') tbs.lib
if($LASTEXITCODE-ne 0){throw 'Game stand-in compile failed'}
& python (Join-Path $PSScriptRoot 'test-dropin-loader.py') $build $Scratch
if($LASTEXITCODE-ne 0){throw 'Standalone loader fixtures failed'}
