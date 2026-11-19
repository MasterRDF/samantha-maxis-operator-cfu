"""Build the v0.2 release ZIP. The ZIP's top level IS the contents of cod25 (tbs.dll, the five fastfiles, kronos/),
so players open it and drag everything straight into their cod25 folder.

Inputs (local build outputs, not tracked in git):
  --base     a previous release's cod25 folder (tbs.dll, maxis-runtime.dll and the jp_/eng_/ww_/srv_ fastfiles)
  --pjs      folder with the v0.2 samantha_maxis_operator_cfu.ff and its test mod.manifest.json (Tacit + Tech Guardian)
  --voice    folder of Kronos voice overrides (hash_<sample>.ogg)
Tracked inputs: package/dropin/{mod.json, lua/, images/}, licenses/, THIRD-PARTY-NOTICES.md.
Writes package/dropin/{mod.manifest.json, dropin.json} (release metadata only differs from the tested manifest) and
dist/Samantha-Maxis-Operator-CFU-v<version>.zip plus its SHA256SUMS file.
"""
import argparse, hashlib, json, shutil, zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
NAME = 'samantha_maxis_operator_cfu'
VERSION = '0.2'
sha = lambda b: hashlib.sha256(b).hexdigest()


def release_manifest(tested: bytes) -> bytes:
    """Version strings only; every file hash, size and memory figure stays exactly as tested."""
    text = tested.decode('utf-8')
    swaps = [('"Samantha Maxis Operator (CFU) v0.2 test (PJs)"', f'"Samantha Maxis Operator (CFU) v{VERSION}"'),
             ('"0.2-pjs-test"', f'"{VERSION}"'),
             ('"Karma -> Tacit (default Maxis) + Karma -> Tech Guardian/proto (Samantha PJs)"',
              '"Karma -> Tacit (default Maxis) + Karma -> Tech Guardian (Samantha PJs)"')]
    for old, new in swaps:
        assert text.count(old) == 1, old
        text = text.replace(old, new)
    return text.encode('utf-8')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--base', required=True); ap.add_argument('--pjs', required=True); ap.add_argument('--voice', required=True)
    a = ap.parse_args(); base, pjs, voice = Path(a.base), Path(a.pjs), Path(a.voice)
    dropin_dir = HERE / 'package/dropin'
    manifest = release_manifest((pjs / 'mod.manifest.json').read_bytes())
    m = json.loads(manifest)
    old_dropin = json.loads((dropin_dir / 'dropin.json').read_text())
    msha = sha(manifest)
    dropin = dict(old_dropin, activationId=msha[:32], manifestSha256=msha)
    (dropin_dir / 'mod.manifest.json').write_bytes(manifest)
    (dropin_dir / 'dropin.json').write_bytes((json.dumps(dropin, indent=2) + '\n').encode())

    # every fastfile must match the manifest exactly
    ff_src = {f['name']: (pjs if f['name'] == NAME + '.ff' else base) / f['name'] for f in m['files']}
    for f in m['files']:
        b = ff_src[f['name']].read_bytes()
        assert len(b) == f['bytes'] and sha(b) == f['sha256'], 'fastfile mismatch: ' + f['name']

    mod = f'kronos/mods/{NAME}'
    entries = {'tbs.dll': base / 'tbs.dll'}
    entries.update({n: p for n, p in ff_src.items()})
    entries[f'{mod}/mod.json'] = dropin_dir / 'mod.json'
    entries[f'{mod}/native/maxis-runtime.dll'] = base / f'{mod}/native/maxis-runtime.dll'
    entries[f'{mod}/native/mod.manifest.json'] = dropin_dir / 'mod.manifest.json'
    entries[f'{mod}/native/dropin.json'] = dropin_dir / 'dropin.json'
    for sub in ('lua', 'images'):
        for p in sorted((dropin_dir / sub).iterdir()): entries[f'{mod}/{sub}/{p.name}'] = p
    for p in sorted((HERE / 'licenses').iterdir()): entries[f'{mod}/licenses/{p.name}'] = p
    entries[f'{mod}/licenses/THIRD-PARTY-NOTICES.md'] = HERE / 'THIRD-PARTY-NOTICES.md'
    oggs = sorted(voice.glob('hash_*.ogg')); assert len(oggs) > 1000, len(oggs)
    for p in oggs: entries[f'kronos/Assets/Audio/samantha_maxis_voice_v2/{p.name}'] = p

    dist = HERE / 'dist'; dist.mkdir(exist_ok=True)
    zpath = dist / f'Samantha-Maxis-Operator-CFU-v{VERSION}.zip'
    assert not zpath.exists(), 'output exists: ' + str(zpath)
    listing = []
    with zipfile.ZipFile(zpath, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for arc, src in entries.items():
            data = src.read_bytes()
            z.writestr(zipfile.ZipInfo.from_file(src, arc), data, compress_type=zipfile.ZIP_DEFLATED)
            listing.append(dict(path=arc, bytes=len(data), sha256=sha(data)))
    (dist / f'package-files-v{VERSION}.json').write_text(json.dumps(dict(version=VERSION, root='cod25', files=listing), indent=1))
    zsha = sha(zpath.read_bytes())
    (dist / f'SHA256SUMS-v{VERSION}.txt').write_text(f'{zsha}  {zpath.name}\n')
    print(zpath, len(listing), 'files', zpath.stat().st_size, 'bytes', zsha)


if __name__ == '__main__':
    main()
