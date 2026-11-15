# Build and package

The native runtime uses MSVC x64 and C++20. Run `Build-Runtime.ps1` with Visual Studio's C++ tools installed. Release compilation uses warnings as errors, Control Flow Guard, ASLR and NX compatibility. MinHook is built from the included source. No game process is started.

The converted fastfiles are release inputs, not reconstructed from this repository alone. They were generated from the user's original game assets through the local extraction, rig adaptation, native serialization and material pipeline. The exact expected fastfile hashes are recorded in `package/mod.manifest.json`.

To assemble a Windows archive, provide the verified five fastfiles, the built runtime, and the BO7 Linker CLI (with its .NET dependency files) to `Package-Release.ps1`. The packager verifies fastfile sizes/hashes and writes `release-files.json` for installation integrity checks. It excludes local installation state and the game's original DLLs/executable.

`tests/CfuProfileFixture.cpp` exercises the actual stable CFU patch implementation against a mapped copy of a user-supplied original Kronos v1.41 DLL and CFU's operator manifest. The small `tests/cfu-reference` snapshot is used only by the offline fixture; it is not installed into BO7. The fixture requires the include paths `src/runtime` and `tests/cfu-reference`, and links `tests/cfu-reference/ops_patch.cpp`.

Example in an MSVC x64 developer shell:

```powershell
cl /nologo /std:c++20 /W4 /WX /EHsc /MT /O2 /DNOMINMAX /DWIN32_LEAN_AND_MEAN /Isrc/runtime /Itests/cfu-reference tests/CfuProfileFixture.cpp tests/cfu-reference/ops_patch.cpp /Fe:build/cfu-tests.exe /Fo:build/
build/cfu-tests.exe 'D:/BO7/cod25/XINPUT9_1_0.dll' 'D:/BO7/cod25/kronos/mods/carry_forward_unleashed/mwcf_operators.dat'
```

Run the combined gameplay gate in `COMPATIBILITY.md` before publishing a release.
