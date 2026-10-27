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
for axis in (0,1):
    exe=out/f'motor_profiles_{axis}.exe'
    subprocess.run([str(a.zig),'cc','-std=c11','-DCONTROL_DUAL_AXIS=0','-O1','-UNDEBUG','-Wall','-Wextra','-Werror',
        f'-DPCB_TEST_AXIS={axis}','-I',str(root/'Core/Inc'),
        str(root/'Tests/test_motor_profiles.c'),str(root/'Core/Src/position_control.c'),
        '-lm','-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
exe=out/'small_moves.exe'
subprocess.run([str(a.zig),'cc','-std=c11','-DCONTROL_DUAL_AXIS=0','-O1','-UNDEBUG','-Wall','-Wextra','-Werror',
    '-DPCB_TEST_AXIS=1','-DCONTROL_MOTION_ENABLED=1','-I',str(root/'Core/Inc'),
    str(root/'Tests/test_small_moves.c'),str(root/'Core/Src/position_control.c'),
    '-lm','-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
for enabled,current_pi in ((0,0),(1,0),(1,1)):
    exe=out/f'position_{enabled}_{current_pi}.exe'
    subprocess.run([str(a.zig),'cc','-std=c11','-DCONTROL_DUAL_AXIS=0','-O1','-UNDEBUG','-Wall','-Wextra','-Werror',
        '-DPCB_TEST_AXIS=0',f'-DCONTROL_MOTION_ENABLED={enabled}',f'-DCONTROL_CURRENT_PI_ENABLED={current_pi}','-I',str(root/'Core/Inc'),
        str(root/'Tests/test_position.c'),str(root/'Core/Src/position_control.c'),
        str(root/'Core/Src/demo_protocol.c'),'-lm','-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
for name,defines,sources in (
        ('system_fsm',['-DCONTROL_DUAL_AXIS=1'],['Tests/test_system_fsm.c','Core/Src/system_fsm.c']),
        ('target_protocol',['-DCONTROL_DUAL_AXIS=1'],['Tests/test_target_protocol.c','Core/Src/demo_protocol.c']),
        ('cal_store',['-DCONTROL_DUAL_AXIS=1','-DCONTROL_MOTION_ENABLED=1'],
         ['Tests/test_cal_store.c','Core/Src/cal_store.c','Core/Src/position_control.c']),
        ('cal_store_tilt_zero',['-DCONTROL_DUAL_AXIS=1','-DCONTROL_MOTION_ENABLED=1','-DCAL_RESTORE_ZERO_TILT=1'],
         ['Tests/test_cal_store.c','Core/Src/cal_store.c','Core/Src/position_control.c']),
        ('homing',['-DCONTROL_DUAL_AXIS=0','-DPCB_TEST_AXIS=1','-DCONTROL_MOTION_ENABLED=1'],
         ['Tests/test_homing.c','Core/Src/position_control.c'])):
    exe=out/f'{name}.exe'
    subprocess.run([str(a.zig),'cc','-std=c11',*defines,'-O1','-UNDEBUG','-Wall','-Wextra','-Werror',
        '-I',str(root/'Core/Inc'),*[str(root/s) for s in sources],'-lm','-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
subprocess.run([sys.executable,'-m','unittest','discover','-s',str(root/'Tests'),'-p','test_client*.py','-v'],check=True)
