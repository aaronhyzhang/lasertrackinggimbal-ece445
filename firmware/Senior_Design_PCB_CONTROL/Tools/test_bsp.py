"""Windows MMIO-backed tests of actual acquisition and network with scripted HAL."""
import argparse
from pathlib import Path
import subprocess
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--zig',type=Path,required=True);a=p.parse_args()
root=Path(__file__).resolve().parents[1];out=root/'build/host-tests';out.mkdir(exist_ok=True,parents=True)
flags=['-std=c11','-O1','-UNDEBUG','-Wall','-Wextra','-Werror','-Wno-unused-parameter',
       '-Wno-pointer-to-int-cast','-Wno-int-to-pointer-cast','-DUSE_HAL_DRIVER','-DSTM32H743xx']
for inc in ('Core/Inc','Core/Src','Drivers/STM32H7xx_HAL_Driver/Inc','Drivers/CMSIS/Include',
            'Drivers/CMSIS/Device/ST/STM32H7xx/Include','Middlewares/LwIP/src/include'):
    flags+=['-I',str(root/inc)]
cases=[]
for motion,source in ((0,0),(1,0),(1,1)):
    cases.append((f'app_{motion}_{source}',[f'-DCONTROL_MOTION_ENABLED={motion}',f'-DCONTROL_SOURCE_ETHERNET={source}'],
                  [root/'Tests/test_app.c',root/'Core/Src/position_control.c']))
for axis in (0,1):
    for revision in ('V','Y'):
        for current_pi in (0,1):
            cases.append((f'acquisition_{axis}_{revision}_{current_pi}',
                          [f'-DPCB_TEST_AXIS={axis}',f'-DTEST_ADC_REVISION=REV_ID_{revision}',
                           f'-DCONTROL_CURRENT_PI_ENABLED={current_pi}'],
                          [root/'Tests/test_acquisition.c',root/'Core/Src/sense_math.c']))
lwip=list((root/'Middlewares/LwIP/src/core').rglob('*.c'))
cases.append(('network',['-DCONTROL_SOURCE_ETHERNET=1'],
              [root/'Tests/test_network.c',root/'Core/Src/demo_protocol.c',*lwip,root/'Middlewares/LwIP/src/netif/ethernet.c']))
for name,defines,sources in cases:
    exe=out/f'{name}.exe'
    subprocess.run([str(a.zig),'cc',*flags,*defines,*map(str,sources),'-lm','-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
