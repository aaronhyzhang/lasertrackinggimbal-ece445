"""Compare real board/vendor timer initialization in private Windows RAM; no hardware."""
import argparse
import json
import pathlib
import subprocess
import sys

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--zig',type=pathlib.Path,required=True)
    p.add_argument('--sync',type=int,choices=[0,1],default=1)
    args=p.parse_args()
    if sys.platform!='win32': p.error('Requires Windows x64 for private fixed-address register RAM.')
    root=pathlib.Path(__file__).resolve().parents[1]
    out=root/'build/host-tests';out.mkdir(parents=True,exist_ok=True)
    results=[]
    for axis in (0,1):
        for stage in (1,3):
            flags=['-std=c11','-O1','-UNDEBUG','-Wall','-Wextra','-Werror',
                   '-Wno-unused-parameter','-Wno-pointer-to-int-cast','-Wno-int-to-pointer-cast',
                   '-DUSE_HAL_DRIVER','-DSTM32H743xx',f'-DPCB_STAGE={stage}',
                   f'-DPCB_TEST_AXIS={axis}','-DPCB_BENCH_CAPTURE_DRIVER_FAULT=0']
            flags += [f'-DPCB_PWM_SYNC_START={args.sync}']
            for inc in ('Core/Inc','Core/Src','Drivers/STM32H7xx_HAL_Driver/Inc',
                        'Drivers/CMSIS/Include','Drivers/CMSIS/Device/ST/STM32H7xx/Include'):
                flags+=['-I',str(root/inc)]
            sources=[root/'Tests/test_timer_bsp.c',root/'Tests/timer_unused_hal.c',root/'Core/Src/drv8304.c']
            sources += [root/'Drivers/STM32H7xx_HAL_Driver/Src'/f'stm32h7xx_hal_{name}.c'
                        for name in ('gpio','tim','tim_ex')]
            binary=out/f'test_timer_sync{args.sync}_{axis}_{stage}.exe'
            subprocess.run([str(args.zig),'cc',*flags,*map(str,sources),'-o',str(binary)],check=True)
            result=subprocess.run([str(binary)],capture_output=True,text=True)
            if result.returncode:
                print(result.stdout,end='');print(result.stderr,end='',file=sys.stderr)
                result.check_returncode()
            print(result.stdout.strip(),flush=True)
            row=json.loads(result.stdout);row['sync_start']=args.sync;results.append(row)
    (out/f'timer_compare_sync{args.sync}_results.json').write_text(json.dumps(results,indent=2)+'\n')
if __name__=='__main__':main()
