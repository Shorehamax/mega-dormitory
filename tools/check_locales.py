"""Check locale parity and the literal menu/journal keys used by native code."""
from pathlib import Path
import json
import re
import sys
root = Path(__file__).resolve().parents[1]
tables = {lang: json.loads((root / 'assets' / 'locales' / f'{lang}.json').read_text()) for lang in ('en', 'fr', 'zh')}
errors = []
for lang, table in tables.items():
    if set(table) != set(tables['en']):
        errors.append(f'{lang}: key set differs from English')
for source in (root / 'src').glob('*.cpp'):
    for match in re.finditer(r'\b(?:t|log)\("([^"\n]+)"\s*[,)]', source.read_text()):
        if match[1] not in tables['en']:
            errors.append(f'{source.name}: missing {match[1]}')
for error in sorted(set(errors)):
    print(error, file=sys.stderr)
if errors:
    sys.exit(1)
print(f'{len(tables["en"])} matching keys; literal menu and journal references resolve')
