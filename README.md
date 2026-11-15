# Samantha Maxis Operator (CFU) v0.1

Samantha Maxis's default Warzone 1 outfit for local BO7 Zombies on stable Kronos v1.41. Select **Karma -> Tacit** for Maxis's body, head and first-person arms, with adapted BO7 skin shading and corrected eye whites.

## Drag-and-drop installation

1. Download the [v0.1 drag-and-drop ZIP](https://github.com/MasterRDF/samantha-maxis-operator-cfu/releases/tag/v0.1) and extract it.
2. Close BO7 and every Kronos window.
3. Copy **the contents of the ZIP's `cod25` folder into your own `cod25` folder**, beside `cod25-cod.exe`. Merge the `kronos` folder.
4. In Kronos's Library, enable **Samantha Maxis Operator (CFU)**. Launch BO7 normally and select **Karma -> Tacit** in Zombies.

No installer, command script, manual Attach step or .NET runtime is required. The included `tbs.dll` starts the dedicated Maxis runtime automatically. CFU's `hid.dll`, Ztron's `pdh.dll` and Kronos's `XINPUT9_1_0.dll` remain unchanged.

If you used the earlier installer distribution, uninstall it using your original extracted folder **before** copying this edition. If you already have another mod's `tbs.dll`, keep a backup and resolve that loader conflict before replacing it. This package replaces the old BO7 Linker TBS starter; it does not combine multiple TBS loaders.

## On/off and removal

Use the **Samantha Maxis Operator (CFU)** Library switch and fully restart BO7 after changing it. Disabling it prevents the native Maxis runtime from loading.

To remove this edition, close BO7/Kronos and delete:

- `tbs.dll` supplied by this package, restoring your backed-up TBS loader if you had one.
- `kronos/mods/samantha_maxis_operator_cfu/`.
- The five `samantha_maxis_operator_cfu.ff` files beside the executable: unprefixed, `jp_`, `eng_`, `ww_` and `srv_`.

The runtime uses its own configuration inside the Maxis mod folder. It does not replace shared `active-mod.json`, `installed.json`, `mods.json` or launcher settings. One native package runtime can hold the shared runtime lock at a time; concurrent generic package activations are refused.

## Compatibility and scope

- Windows x64; English BO7 cod25 CL29269900 and stable Kronos v1.41. Exact original build hashes are enforced. Kronos dev47 and other game/client builds are unsupported.
- CFU and Ztron are separate mods, not bundled. The verified target is CFU `1.5.4-stable-renames-trial-02` and Ztron Guns `88c`.
- The automatic-start edition passed user preview/local Zombies testing with CFU, Ztron, Carry-forward and BO6 Operators enabled, without the old installer runtime or manual attachment.
- Keep Karma/Tacit selected. A BO6/MW operator override selects a different model.
- Karma's menu identity, portrait and voice remain. Facial-expression retargeting, multiplayer, dedicated servers and other languages are outside the validated scope.
- Skin shading uses native BO7 materials and approximate auxiliary-map adaptation; it does not reproduce every original Warzone shader or lighting condition.

## Source and release contents

Source, tests and packaging metadata are in this repository. The Windows release ZIP contains the converted fastfiles and native loaders. Original game executables, Kronos/CFU/Ztron DLLs, private settings, logs and backups are excluded.

See [BUILD.md](BUILD.md), [COMPATIBILITY.md](COMPATIBILITY.md), and [CHANGELOG.md](CHANGELOG.md).

## Credits

Port and adaptation: **MasterRDF**. Original character/game assets: Activision/Treyarch and the Call of Duty teams. CFU's operator integration incorporates Maverick's original operator/skin list and patch design. Ztron Guns is by Ztron and distributed separately.

Runtime and packaging work builds on the local BO7 Linker project. Third-party licenses are retained under `src/runtime/minhook`, `src/runtime/third-party` and `licenses`; see [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md). Game assets are not covered by those source licenses.
