"""Portable controller/protocol tests, including commissioning and active builds."""
import argparse
from pathlib import Path
import subprocess
import sys

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--zig',type=Path,required=True)
a=p.parse_args()
root=Path(__file__).resolve().parents[1]
out=root/'build/host-tests';out.mkdir(exist_ok=True,parents=True)
for enabled,current_pi in ((0,0),(1,0),(1,1)):
    exe=out/f'position_{enabled}_{current_pi}.exe'
    subprocess.run([str(a.zig),'cc','-std=c11','-O1','-UNDEBUG','-Wall','-Wextra','-Werror',
        f'-DCONTROL_MOTION_ENABLED={enabled}',f'-DCONTROL_CURRENT_PI_ENABLED={current_pi}','-I',str(root/'Core/Inc'),
        str(root/'Tests/test_position.c'),str(root/'Core/Src/position_control.c'),
        str(root/'Core/Src/demo_protocol.c'),'-lm','-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
subprocess.run([sys.executable,'-m','unittest','discover','-s',str(root/'Tests'),'-p','test_client.py','-v'],check=True)
