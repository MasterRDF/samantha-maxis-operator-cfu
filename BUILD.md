# Build and package

For the current drop-in edition, run `Build-DropIn.ps1` with Visual Studio C++ x64 tools installed. It builds `build/dropin/maxis-runtime.dll` and a `tbs.dll` startup loader pinned to that exact runtime hash. C++20, static C runtime, Control Flow Guard, ASLR and NX are enabled. Building source requires MSVC; users installing the ZIP do not need build tools or .NET.

Run `Package-DropIn.ps1 -FastfileFolder <verified-fastfile-folder>` to assemble the v0.1 ZIP. Expected fastfile hashes are in `package/dropin/mod.manifest.json`. Converted game assets are release inputs; they are not reconstructed from this repository alone.

The native runtime uses private `dropin.json` and `mod.manifest.json` files under `kronos/mods/samantha_maxis_operator_cfu/native`. Startup checks the Library switch and the runtime DLL hash before loading. The main fastfiles remain beside the game executable because the existing native loading path resolves them there.

Tests include standalone fake-game loader/forwarding cases (`tests/build-dropin-tests.ps1`), private configuration selection and Library switches, the pinned CFU callback fixture, and the inherited 117 passive-sharing checks. The fake-game fixtures do not launch or attach to BO7.

Language tests cover absent/blank overrides, explicit conflicts and BOM/encoding normalization. `tests/build-cfu-tests.ps1` additionally accepts the original Kronos DLL, CFU operator manifest and released operator DLL paths to verify the public build gate, guard template and callback forwarding against private fixtures.
