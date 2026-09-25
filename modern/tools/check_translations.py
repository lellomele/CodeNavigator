"""Check translation coverage and numbered placeholders in the application catalog."""
import json
import re
from pathlib import Path
base=Path(__file__).resolve().parents[1]
catalog=json.loads((base/'gui/translations.json').read_text(encoding='utf-8'))
for source,translations in catalog.items():
    assert set(translations)=={'en','fr','de'},source
    expected=sorted(re.findall(r'%\d+',source))
    for language,text in translations.items():
        assert text.strip(),(source,language)
        assert sorted(re.findall(r'%\d+',text))==expected,(source,language)
for path in (base/'gui').glob('*.cpp'):
    for match in re.finditer(r'ui\(QStringLiteral\(\s*((?:"(?:[^"\\]|\\.)*"\s*)+)\)',path.read_text(encoding='utf-8')):
        source=''.join(json.loads(part) for part in re.findall(r'"(?:[^"\\]|\\.)*"',match[1]))
        assert source in catalog,(path.name,source)
print(f'{len(catalog)} messages: English, French and German complete; placeholders preserved')
