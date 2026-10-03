"""Repeatable local measurement of text indexing and search."""
import json
from pathlib import Path
import subprocess
import tempfile
import time

base=Path(__file__).resolve().parents[1]
engine=base/'core/target/release/sn-index.exe'
with tempfile.TemporaryDirectory(prefix='navigator-benchmark-') as folder:
    folder=Path(folder);root=folder/'src';root.mkdir();db=folder/'cache/index.sqlite'
    for n in range(1000):
        text=f'pub fn item_{n:04}() -> usize {{\n    {n}\n}}\n'+('// A representative source comment with UTF-8: caffè.\n'*100)
        (root/f'module_{n:04}.rs').write_text(text,encoding='utf-8')
    args=['index','--root',str(root),'--db',str(db),'--parsers',str(base/'third_party/parsers')]
    def measure(command):
        started=time.perf_counter();p=subprocess.run([str(engine),*command],capture_output=True,check=True)
        return dict(wall_ms=round((time.perf_counter()-started)*1000,1),result=json.loads(p.stdout.splitlines()[-1]))
    result=dict(files=1000,bytes=sum(p.stat().st_size for p in root.iterdir()),first_index=measure(args),unchanged_index=measure(args),literal=measure(['grep','--db',str(db),'--pattern','item_0499']),regex=measure(['grep','--db',str(db),'--pattern',r'item_0[45]\d{2}','--mode','regex']))
    phases=[]
    for log in sorted((db.parent/'logs').glob('*.jsonl')):
        events=[json.loads(line) for line in log.read_text(encoding='utf-8').splitlines()]
        origin=events[0]['time_unix_ms']
        phases.append({e['event']: e['time_unix_ms']-origin for e in events if e['event'] in ['scan','published']})
    result['phases']=phases
    (base/'test-output/benchmark.json').write_text(json.dumps(result,indent=2,ensure_ascii=False),encoding='utf-8')
    print(json.dumps({k:v['wall_ms'] if isinstance(v,dict) else v for k,v in result.items()}))
