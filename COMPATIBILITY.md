# Compatibility

| Component | Target | Evidence |
| --- | --- | --- |
| BO7 | cod25 CL29269900, English | Preview and local Zombies appearance accepted |
| Kronos | Stable v1.41 | Exact original DLL hash required |
| CFU | Public 1.6.2 and local 1.5.4 trial | 282 callback/build checks; external affected-user gameplay accepted |
| Ztron Guns | 88c | Loaded alongside Maxis and CFU; combined gameplay accepted |

## Native callback conflict

CFU's operator companion changes six bytes at Kronos RVA `0xb8595` to enter a targeted stale-string guard. The earlier Maxis diagnostic runtime required the stock callback tail, so CFU would cause a profile refusal before Maxis loaded.

The release runtime accepts either the existing stock profile or a separate CFU profile. CFU acceptance requires the correctly located, loaded operator companion with either verified SHA256 `3d692ab79d38bd3b69979bdc2a0a2a0d6c5bf51ecc4353ad5f93f179999f8957` (local trial) or `3d0dffabce075a961c090bcb5488ed97f0af8782a7d7e4cc9830fc12564c8744` (public release), the unchanged callback entry/initializer and tail suffix, all 247 guard bytes, and all four expected relocated destinations. Unexpected changes are refused. The guard and operator tables are not modified; the error observer forwards the original callback arguments, return value and Windows error state.

The offline fixture maps a copy of the original Kronos file without executing it, applies CFU's actual stable patch implementation, reproduces the old profile failure, and verifies the new profile. Every single-byte guard mutation and selected callback changes are refused. The fixture also verifies error forwarding and that only the existing callback cell changes when its observer is installed. No BO7 process is attached by these tests.

## Pinned files

BO7 executable: `b0b12b0e7ec25ce855ea6e9d042fc8f390b8d48097cb10e0899e8334f35b6db8`.

Kronos v1.41: `88a52103c5555b2760f1e7ad13a594e8ef0440f007b45a10bddc61dda234fcfb`.

The converted fastfiles remain byte-identical to the accepted full-body visual build. Compatibility changes affect loading and release metadata.

## Live validation procedure

With CFU and Ztron enabled and Carry-forward on, verify Maxis in preview and in a local match; use an MWII/MWIII CFU gun and the Ztron DG-2 or Jet Gun; inspect first-person hands and third-person appearance; return to the lobby and start another match. Record the actual result before marking combined compatibility verified or publishing v0.1.


## Carry-forward package-slot conflict

Kronos reserves request 4 and its name cell for `global_stream_cfwd_mp`. The isolated Maxis loader also used that slot while Carry-forward was off. The first combined run correctly refused the occupied slot and displayed native Karma. A bounded read-only capture confirmed request 4's native owner, loaded state and package name.

The shared path reuses the earlier local Thompson runtime's passive-sharing implementation. It observes Kronos's natural request publication and supplies a private two-name descriptor containing the existing Carry-forward package followed by Maxis. It preserves the native name-cell pointer, original package and request flags, forwards the original write, refuses busy/unexpected states, and cleans only its own settled idle leftovers. It does not force an active request to unload or reuse an unrelated request slot. Frontend preloading is explicitly enabled in this package's manifest so the appearance can be present before spawning.

All 117 inherited sharing, busy-state, preload, acknowledgement and cleanup checks pass against the adapted runtime headers. The combined gameplay retest passed by user report with both mods, Carry-forward and BO6 Operators enabled. Logs independently confirm the shared package reached native loaded state twice.

## v0.1 drag-and-drop startup

The dedicated `tbs.dll` startup loader forwards Windows TBS exports to the system DLL and loads `kronos/mods/samantha_maxis_operator_cfu/native/maxis-runtime.dll` only when its Library entry is enabled. It verifies the exact runtime DLL hash before attachment. CFU's HID loader, Ztron's PDH loader and Kronos's XInput DLL are unchanged.

The runtime selects its own portable configuration and manifest, with no installed.json or shared active-mod.json requirement. Its existing global lifetime lock still refuses another active generic package runtime. The same model/texture fastfiles and native Carry-forward publication implementation are retained.

Offline validation covers 15 Library/private-configuration checks and 10 complete fake-game startup cases, including disabled/missing mods, altered/missing runtime DLLs, invalid switches, ignored legacy files, other executables and system TBS forwarding. The production runtime and loader compile with warnings as errors. User testing confirms automatic startup and working Maxis in preview/local Zombies with CFU, Ztron, Carry-forward and BO6 Operators enabled. The old generic runtime DLL and shared activation file were absent during the successful test. A first launch ended during base-pack loading before Maxis publication; a subsequent launch passed. Its evidence is retained separately, and causation was not established.

## Confirmed external startup fixes in v0.1

An affected English installation had no force_language.txt override. The earlier runtime read an empty value and refused startup. The override is now optional: absent/blank uses the package's declared language, while the original localized game fastfile hash check remains mandatory. Explicit conflicting language overrides are still refused; UTF BOMs, whitespace and case are normalized. No game language setting is created or changed.

After that fix, the same user reached the callback-profile gate. The public CFU 1.6.2 release uses operator companion SHA256 `3d0dffabce075a961c090bcb5488ed97f0af8782a7d7e4cc9830fc12564c8744`, whereas the original gate allowed only the local trial SHA256 `3d692ab79d38bd3b69979bdc2a0a2a0d6c5bf51ecc4353ad5f93f179999f8957`. The actual public ZIP contains the identical 247-byte guard template and original operator-manifest witness. Both exact companion hashes are now supported; module location, all callback/guard bytes and the four relocation targets remain checked. Unknown builds are still refused.

The affected user confirms the combined replacement DLL pair works. Validation passes 282 callback/build/guard checks, 117 shared-publication checks, 16 private-configuration checks, 16 language cases and 10 startup/forwarding cases (441 total). These fixes are included in the full v0.1 download; a separate patch is not required.
