"""Run the drop-in startup loader against private stand-in executables only."""
from pathlib import Path
import json
import shutil
import subprocess
import sys

build, scratch = map(lambda p: Path(p).resolve(), sys.argv[1:3])
scratch.mkdir(parents=True, exist_ok=False)
cases = [
    ('enabled-default', {}, True, 'Started dedicated Maxis runtime'),
    ('enabled-explicit', {'switch': True}, True, 'Started dedicated Maxis runtime'),
    ('disabled', {'switch': False}, False, 'disabled or absent'),
    ('disabled-zero', {'switch': 0}, False, 'disabled or absent'),
    ('missing-mod', {'missing_mod': True}, False, 'disabled or absent'),
    ('missing-runtime', {'missing_runtime': True}, False, 'runtime DLL is missing'),
    ('changed-runtime', {'changed_runtime': True}, False, 'checksum differs'),
    ('malformed-settings', {'malformed': True}, False, 'Refused:'),
    ('other-program', {'other_program': True}, False, None),
    ('legacy-files-ignored', {'legacy': True}, True, 'Started dedicated Maxis runtime'),
]
for name, options, expected, text in cases:
    game = scratch / name
    native = game / 'kronos/mods/samantha_maxis_operator_cfu/native'
    native.mkdir(parents=True)
    exe = 'other-tool.exe' if options.get('other_program') else 'cod25-cod.exe'
    shutil.copy2(build / 'cod25-cod.exe', game / exe)
    shutil.copy2(build / 'XINPUT9_1_0.dll', game)
    shutil.copy2(build / 'tbs.dll', game)
    if not options.get('missing_mod'):
        (native.parent / 'mod.json').write_text('{}')
    if not options.get('missing_runtime'):
        shutil.copy2(build / 'maxis-runtime.dll', native)
    if options.get('changed_runtime'):
        with (native / 'maxis-runtime.dll').open('ab') as file:
            file.write(b'tamper')
    if 'switch' in options:
        (game / 'kronos/mods.json').write_text(json.dumps({'enabled': {'samantha_maxis_operator_cfu': options['switch']}}))
    if options.get('malformed'):
        (game / 'kronos/mods.json').write_text('invalid JSON')
    if options.get('legacy'):
        shared = game / 'kronos/runtime'
        shared.mkdir()
        (shared / 'active-mod.json').write_text('invalid legacy activation')
        (game / 'bo7-runtime.dll').write_bytes(b'invalid legacy DLL')
    result = subprocess.run([str(game / exe)], cwd=game, capture_output=True, text=True, timeout=8)
    marker = game / 'runtime-attached.txt'
    logfile = game / 'kronos/Logs/maxis-loader.log'
    log = logfile.read_text(encoding='utf-8') if logfile.exists() else ''
    assert result.returncode == (0 if expected else 1), (name, result.stdout, result.stderr, log)
    assert 'through the starter: right' in result.stdout, (name, result.stdout)
    assert marker.exists() == expected, (name, log)
    if expected:
        assert marker.read_text() == 'PASS'
    assert (text in log) if text else not log, (name, log)
    print('PASS:', name)
print('PASS: 10 standalone startup/toggle/checksum/forwarding cases; no actual BO7 launched.')
