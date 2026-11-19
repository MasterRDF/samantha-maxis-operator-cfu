# Changelog

## v0.2 - 2026-11-19

- Added a new, original skin for Maxis under the "Tech Guardian" skin for Karma. It's a skin that combines the body of the Grey in Sam's PJ's skin and Sam's head model from bo6. It also uses the hair mesh from Sam's Cold War model.
- Replaced all (there might be a few outliars) of karma's voice lines with Sam's Cold War lines and silenced any karma voice lines that Sam doesn't have proper replacement lines for (i.e. unique BO7 equipment and scorestreaks).
- Replaced Karma's operator portrait with a portrait of Sam.
- Packaging: the ZIP's top level is now the contents of `cod25`, ready to drag in. Built by `Package-Release.py`.

## v0.1 - 2026-11-15

- Default Samantha Maxis body, both head variants and first-person arms on Karma/Tacit.
- Native BO7 skin shading with restored source normal/gloss detail, purple irises and brighter eye whites.
- Passive shared loading beside Carry-forward, preserving CFU's operator guard.
- Automatic startup through a dedicated TBS loader; no installer, manual Attach step or .NET requirement.
- Private configuration and a Kronos Library on/off switch, applied after a full restart.
- User gameplay accepted with CFU, Ztron, Carry-forward and BO6 Operators enabled.
- Drag-and-drop packaging completely replaces the initial installer distribution of this same version.

- Fixed English startup when the optional force_language.txt override is missing or blank.
- Added the verified public CFU 1.6.2 operator companion without weakening callback validation.
- Confirmed the combined fix on the affected external installation; retained version v0.1.
