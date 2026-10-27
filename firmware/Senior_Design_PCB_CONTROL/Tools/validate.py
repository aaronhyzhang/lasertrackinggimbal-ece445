"""Run software tests and ARM build matrix; record hashes, without flashing."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

p=argparse.ArgumentParser(description=__doc__);p.add_argument('--zig',type=Path,required=True);a=p.parse_args()
root=Path(__file__).resolve().parents[1];logs=root/'build/validation';logs.mkdir(parents=True,exist_ok=True)
records=[]
def run(name,arguments):
    path=logs/f'{name}.log'
    with path.open('w',encoding='utf-8') as f:
        r=subprocess.run([sys.executable,*map(str,arguments)],stdout=f,stderr=subprocess.STDOUT,text=True)
    print(f'{name}: {"PASS" if r.returncode==0 else "FAIL"}',flush=True)
    if r.returncode:
        print(path.read_text()[-8000:]);raise SystemExit(r.returncode)
    records.append({'name':name,'command':list(map(str,arguments)),'log_sha256':hashlib.sha256(path.read_bytes()).hexdigest()})
run('portable',[root/'Tools/test.py','--zig',a.zig.resolve()])
run('bsp',[root/'Tools/test_bsp.py','--zig',a.zig.resolve()])
for config,axis,motion,source in [('Debug',0,'off','pot'),('Release',0,'off','pot'),
                                  ('Debug',0,'on','pot'),('Release',0,'on','ethernet'),
                                  ('Release',1,'on','pot'),('Release',1,'on','ethernet')]:
    run(f'{config}_axis{axis}_{motion}_{source}',[root/'Tools/build.py','--config',config,'--axis',axis,'--motion',motion,'--source',source])
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
sources={str(f.relative_to(root)).replace('\\','/'):sha(f) for folder in ('Core','Drivers','Tools','Tests')
         for f in (root/folder).rglob('*') if f.is_file() and f.suffix in ('.c','.h','.s','.py')}
sources.update({name:sha(root/name) for name in ('.project','.cproject','STM32H743ZITX_FLASH.ld')})
elfs={str(f.relative_to(root)).replace('\\','/'):sha(f) for f in (root/'build').rglob('*.elf')}
evidence={'date':'2026-10-26','hardware_validated':False,'current_pi_default':False,
          'tests_and_builds':records,'source_sha256':sources,'elf_sha256':elfs}
(root/'docs/validation_evidence.json').write_text(json.dumps(evidence,indent=2)+'\n')
print('All software checks passed. Hardware commissioning remains required.',flush=True)
