#!/usr/bin/env python3
"""Verify bundled third-party art against the recorded source checksums."""
from pathlib import Path
import hashlib
import json

root = Path(__file__).resolve().parents[1]
verified = 0
for catalogue in (root / 'assets/models/manifest.json', root / 'assets/surfaces/manifest.json'):
    for asset in json.loads(catalogue.read_text()):
        if asset['license'] != 'CC0-1.0':
            raise SystemExit(f"Unapproved asset licence: {asset['id']}")
        for entry in asset['files']:
            path = (root / entry['file']).resolve()
            if not path.is_relative_to(root):
                raise SystemExit('Manifest path escapes the project')
            if hashlib.sha256(path.read_bytes()).hexdigest() != entry['sha256']:
                raise SystemExit(f'Asset checksum mismatch: {path}')
            verified += 1
catalogue = root / 'assets/vehicles/kenney/source.json'
pack = json.loads(catalogue.read_text())
if pack['license'] != 'CC0-1.0':
    raise SystemExit('Unapproved vehicle licence')
for entry in pack['files']:
    path = (catalogue.parent / entry['path']).resolve()
    if not path.is_relative_to(catalogue.parent.resolve()):
        raise SystemExit('Vehicle path escapes asset pack')
    if hashlib.sha256(path.read_bytes()).hexdigest() != entry['sha256']:
        raise SystemExit(f'Vehicle checksum mismatch: {path}')
    verified += 1
catalogue = root / 'assets/characters/quaternius/source.json'
pack = json.loads(catalogue.read_text())
if pack['license'] != 'CC0-1.0':
    raise SystemExit('Unapproved character licence')
for entry in pack['files']:
    path = (catalogue.parent / entry['file']).resolve()
    if not path.is_relative_to(catalogue.parent.resolve()):
        raise SystemExit('Character path escapes asset pack')
    if hashlib.sha256(path.read_bytes()).hexdigest() != entry['sha256']:
        raise SystemExit(f'Character checksum mismatch: {path}')
    verified += 1
print(f'Verified {verified} downloaded files across Poly Haven, Kenney and Quaternius CC0 assets.')
