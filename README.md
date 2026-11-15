# Samantha Maxis Operator (CFU) v0.1

Samantha Maxis's default outfit from Warzone 1, adapted for local Black Ops 7 Zombies on Kronos v1.41. Select **Karma → Tacit** to use Maxis.

The port includes her body, head, goggles/headset, outfit and first-person arms, with native BO7 skin and eye materials. Her purple irises are retained and her eye whites are deliberately brightened for BO7 lighting.

## Status

User testing confirms Maxis in preview and local Zombies with CFU, Ztron, Carry-forward and BO6 Operators enabled. The shared native request loaded both Carry-forward and Maxis successfully.

## Requirements

- Windows x64 and .NET Runtime 10.
- BO7 `cod25` CL29269900 with stable Kronos v1.41 and English game files.
- The supported executable and Kronos hashes are enforced by the installer/runtime. Other builds, including Kronos dev47, are unsupported.
- CFU and Ztron are separate mods. The compatibility target is CFU `1.5.4-stable-renames-trial-02` with its stable operator companion, and Ztron Guns `88c`.

## Installation

1. Download and extract the [v0.1 Windows release archive](https://github.com/MasterRDF/samantha-maxis-operator-cfu/releases/tag/v0.1). Keep the extracted folder; it holds uninstall backups.
2. Close BO7 and all Kronos windows.
3. Run `Install.cmd` and enter the full path to your `cod25` folder. Existing runtime ownership and original game hashes are checked before any change; conflicting modifications are refused.
4. Launch Kronos/BO7 yourself. Stay at the main menu and run `Attach.cmd` from the extracted release folder before entering Zombies. If BO7 is elevated, run it as administrator and approve Windows's prompt yourself.
5. Enter local Zombies and select **Karma → Tacit**. Allow the runtime to load the appearance package before entering a match.

Run `Attach.cmd` once per BO7 session. If your installation already has a compatible automatic runtime starter, it can attach automatically; use the runtime status/log to confirm before attaching again. The launcher and other mods' DLLs remain in place. This is a native appearance package and requires its runtime; copying its fastfiles into a Kronos Library folder alone is insufficient.

The installer does not change your CFU/Ztron switches or launcher preferences. To test coexistence, enable both Library entries and Carry-forward in Kronos before launching. Keep the native BO7 Karma/Tacit selection; a BO6/MW operator override selects a different model.

## Uninstall

Close BO7 and Kronos, then run `Uninstall.cmd` from the same extracted folder. Restoration checks the installed files and backups before returning the previous runtime/package. If a file changed afterward, restoration stops for review. Keep the `.state` folder until uninstall completes.

## Scope and limitations

- Replaces Karma/Tacit appearance; the menu name, portrait, voice and gameplay identity remain Karma.
- Uses Karma's animations. Facial-expression retargeting is not implemented.
- Skin shading is adapted to BO7, including approximate transfer of native auxiliary maps. It does not reproduce every original Warzone shader or lighting condition.
- This version targets local Zombies on the pinned English build. Multiplayer, dedicated servers, other languages and other clients have not been validated.
- Only one package can own this generic runtime activation at a time. Installation backs up the preceding activation instead of merging packages.

## Repository and release layout

The repository contains the runtime source, compatibility tests, installer scripts and release metadata. Converted fastfiles, runtime binaries and the small linker CLI are distributed in the Windows release archive rather than Git history. Original game executables, Kronos/CFU/Ztron DLLs, private settings, logs and installation backups are excluded.

See [BUILD.md](BUILD.md), [COMPATIBILITY.md](COMPATIBILITY.md), and [CHANGELOG.md](CHANGELOG.md).

## Credits

Port and adaptation: **MasterRDF**. Original character and game assets: Activision/Treyarch and the Call of Duty teams. CFU's operator integration incorporates Maverick's original operator/skin list and patch design. Ztron Guns is by Ztron and is distributed separately.

The runtime/packaging work builds on the local BO7 Linker project. Third-party source licenses are retained under `src/runtime/minhook` and `src/runtime/third-party`; see [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md). Game assets are not covered by those source licenses.

