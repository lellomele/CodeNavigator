"""Validate the packaged application with no development runtime on PATH."""
import argparse
import json
import os
from pathlib import Path
import subprocess

base=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser()
parser.add_argument('--dist', type=Path, default=base/'dist')
args=parser.parse_args()
distribution=args.dist.resolve()
output=base/'test-output/packaged'
output.mkdir(parents=True,exist_ok=True)
env=os.environ.copy()
env['PATH']=r'C:\Windows\System32;C:\Windows'
env.pop('QT_PLUGIN_PATH',None)
env['QT_QPA_PLATFORM']='offscreen'
env['QT_QPA_FONTDIR']='C:/Windows/Fonts'
env['SN_DATA_DIR']=str(output/'profile')
exe=distribution/'source-navigator.exe'
p=subprocess.run([str(exe),'--theme','3','--file',str(distribution/'demo/catalog.cpp'),'--capture',str(output/'startup.png')],env=env,timeout=25,capture_output=True)
assert p.returncode==0,(p.returncode,p.stderr)
assert (output/'startup.png').stat().st_size>10000
before=set((output/'profile/logs').glob('crash-*'))
p=subprocess.run([str(exe),'--diagnostic-crash'],env=env,timeout=25,capture_output=True)
assert p.returncode!=0
crashes=set((output/'profile/logs').glob('crash-*'))-before
assert any(p.suffix=='.dmp' and p.stat().st_size>1000 for p in crashes),crashes
events=[json.loads(p.read_text(encoding='utf-8')) for p in crashes if p.suffix=='.jsonl']
assert any(e['event']=='native_crash' and e['exception']==0xE0424242 for e in events)
(output/'validation.json').write_text(json.dumps(dict(startup='passed',native_crash_log='passed',minidump='passed',isolated_path=True),indent=2),encoding='utf-8')
print('Packaged startup, native crash JSON and minidump: passed')
