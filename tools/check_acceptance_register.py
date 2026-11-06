#!/usr/bin/env python3
"""Verify that the frozen M00 inventory covers every requirement and parity decision."""
import json
from pathlib import Path

root = Path(__file__).resolve().parents[1]
plan = json.loads((root / 'docs/completion-progress.json').read_text())
register = json.loads((root / 'docs/acceptance-register.json').read_text())
requirements = {r['id']: r for r in plan['requirements']}
entries = {r['id']: r for r in register['requirements']}
milestones = {m['id'] for m in plan['milestones']}
assert len(entries) == len(register['requirements']) and set(entries) == set(requirements), 'Requirement coverage mismatch'
scenarios = set()
text = (root / 'docs/acceptance-scenarios.md').read_text()
for rid, entry in entries.items():
    assert entry['owners'] and set(entry['owners']) == set(requirements[rid]['milestones']), f'Invalid owners for {rid}'
    assert entry['scenario'] not in scenarios, f'Duplicate scenario: {rid}'
    scenarios.add(entry['scenario'])
    assert f'id="{entry["scenario"].lower()}"' in text, f'Missing readable scenario: {rid}'
    assert entry['classification'] in {'implemented', 'partial', 'missing', 'cancelled'}, rid
    assert entry['procedure'] and entry['pass_condition'] and entry['sources'], rid
    for source in entry['sources']:
        assert (root / source).is_file(), f'Missing source: {source}'
parity = register['browser_parity']
assert len({p['id'] for p in parity}) == len(parity), 'Duplicate browser parity IDs'
for entry in parity:
    assert entry['owner'] in milestones and entry['scenario'] in scenarios, entry['id']
    assert (root / entry['source']).is_file() and entry['policy'], entry['id']
manifest = json.loads((root / 'docs/prototype-manifest.json').read_text())
assert manifest['snapshots'][0]['files'] == manifest['snapshots'][2]['files'], 'Latest prototype references disagree'
print(f'{len(entries)} requirements, {len(parity)} parity decisions and {len(scenarios)} acceptance scenarios have valid owners and references.')
