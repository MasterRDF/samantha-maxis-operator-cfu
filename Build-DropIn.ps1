$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
# Reuse the existing source build to initialize MSVC and build the same MinHook.
& (Join-Path $PSScriptRoot 'Build-Runtime.ps1')|Out-Null
if(-not $?){throw 'Base runtime build failed'}
$source=Join-Path $PSScriptRoot 'src/runtime'
$loader=Join-Path $PSScriptRoot 'src/dropin'
$build=Join-Path $PSScriptRoot 'build/dropin'
New-Item -ItemType Directory -Path $build -Force|Out-Null
& cl.exe /nologo /std:c++20 /W4 /WX /EHsc /MT /O2 /guard:cf /DBO7_RELEASE /DBO7_DROPIN /LD "/I$source" "/Fe:$build/maxis-runtime.dll" "/Fo:$build/runtime.obj" (Join-Path $source 'runtime.cpp') /link "/DEF:$source/runtime.def" (Join-Path $PSScriptRoot 'build/runtime/minhook.lib') bcrypt.lib advapi32.lib psapi.lib /DYNAMICBASE /NXCOMPAT
if($LASTEXITCODE-ne 0){throw 'Drop-in runtime compilation failed'}
$sha=(Get-FileHash -LiteralPath (Join-Path $build 'maxis-runtime.dll')).Hash.ToLowerInvariant()
[IO.File]::WriteAllText((Join-Path $build 'dropin-runtime-pin.hpp'),('#pragma once'+[Environment]::NewLine+'inline constexpr char MaxisRuntimeSha256[]="'+$sha+'";'+[Environment]::NewLine),[Text.UTF8Encoding]::new($false))
& cl.exe /nologo /std:c++20 /W4 /WX /EHsc /MT /O2 /guard:cf /LD "/I$source" "/I$build" "/Fe:$build/tbs.dll" "/Fo:$build/loader.obj" (Join-Path $loader 'loader.cpp') /link bcrypt.lib /DYNAMICBASE /NXCOMPAT
if($LASTEXITCODE-ne 0){throw 'Drop-in loader compilation failed'}
Get-FileHash -LiteralPath (Join-Path $build 'tbs.dll'),(Join-Path $build 'maxis-runtime.dll') -Algorithm SHA256
