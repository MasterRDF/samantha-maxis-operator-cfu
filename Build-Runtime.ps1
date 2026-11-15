$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$source=Join-Path $PSScriptRoot 'src/runtime'
$build=Join-Path $PSScriptRoot 'build/runtime'
New-Item -ItemType Directory -Path $build -Force|Out-Null
$finder=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if(-not (Test-Path -LiteralPath $finder)){throw 'Install Visual Studio C++ x64 build tools'}
$installation=& $finder -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(-not $installation){throw 'Visual Studio C++ x64 build tools were not found'}
$vcvars=Join-Path $installation 'VC/Auxiliary/Build/vcvars64.bat'
cmd /d /c "`"$vcvars`" >nul && set"|ForEach-Object {if($_ -match '^([^=]+)=(.*)$'){[Environment]::SetEnvironmentVariable($Matches[1],$Matches[2],'Process')}}
if($LASTEXITCODE-ne 0){throw 'MSVC environment initialization failed'}
$minhook=Join-Path $source 'minhook'
$sources=@('buffer.c','hook.c','trampoline.c','hde/hde64.c')|ForEach-Object {Join-Path (Join-Path $minhook 'src') $_}
& cl.exe /nologo /W4 /O2 /MT /c "/I$minhook/include" "/Fo:$build/" @sources
if($LASTEXITCODE-ne 0){throw 'MinHook compilation failed'}
$objects=@('buffer.obj','hook.obj','trampoline.obj','hde64.obj')|ForEach-Object {Join-Path $build $_}
& lib.exe /nologo "/OUT:$build/minhook.lib" @objects
if($LASTEXITCODE-ne 0){throw 'MinHook library build failed'}
& cl.exe /nologo /std:c++20 /W4 /WX /EHsc /MT /O2 /guard:cf /DBO7_RELEASE /LD "/I$source" "/Fe:$build/bo7-runtime.dll" "/Fo:$build/runtime.obj" (Join-Path $source 'runtime.cpp') /link "/DEF:$source/runtime.def" (Join-Path $build 'minhook.lib') bcrypt.lib advapi32.lib psapi.lib /DYNAMICBASE /NXCOMPAT
if($LASTEXITCODE-ne 0){throw 'Release runtime compilation failed'}
Get-FileHash -LiteralPath (Join-Path $build 'bo7-runtime.dll') -Algorithm SHA256

