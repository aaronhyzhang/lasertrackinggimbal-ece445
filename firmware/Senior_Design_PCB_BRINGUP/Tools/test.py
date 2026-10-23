"""Host tests for the actual open-loop core, button app and DRV8304S backend."""
import argparse
import pathlib
import subprocess
import sys

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--cc',default='cc')
    p.add_argument('--zig',type=pathlib.Path)
    args=p.parse_args()
    root=pathlib.Path(__file__).resolve().parents[1]
    out=root/'build/host-tests'; out.mkdir(parents=True,exist_ok=True)
    cc=[str(args.zig),'cc'] if args.zig else [args.cc]
    cases=[('test_motor',3,0,0),('test_driver',3,0,0),('test_driver',3,1,0),
           ('test_driver',2,0,1),('test_driver',2,1,1)]
    cases += [('test_app',s,0,0) for s in range(4)] + [('test_app',3,1,0),('test_app',2,1,1)]
    cases += [('test_axis',2,0,a) for a in range(2)]
    for name,stage,bench,axis in cases:
        binary=out/(f'{name}_{stage}_{bench}_{axis}'+('.exe' if sys.platform=='win32' else ''))
        flags=['-std=c11','-Wall','-Wextra','-Werror',f'-DPCB_STAGE={stage}',
               f'-DPCB_BENCH_ALLOW_CPUV_HISTORY={bench}',f'-DPCB_TEST_AXIS={axis}']
        sources=[root/f'Tests/{name}.c']
        if name=='test_app':
            flags+=['-I',str(root/'Tests/fakes')]
            sources+=[root/'Core/Src/demo_app.c']
        flags+=['-I',str(root/'Core/Inc')]
        if name=='test_driver': sources+=[root/'Core/Src/drv8304.c']
        elif name!='test_axis': sources+=[root/'Core/Src/open_loop.c',root/'Core/Src/modulation.c']
        subprocess.run([*cc,*flags,*map(str,sources),'-lm','-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)
    for stage,axis in [(1,1),(3,1),(2,2)]:
        result=subprocess.run([*cc,'-std=c11','-fsyntax-only','-x','c',
            '-I',str(root/'Core/Inc'),f'-DPCB_STAGE={stage}',f'-DPCB_TEST_AXIS={axis}','-'],
            input='#include "pcb_config.h"\n',text=True,capture_output=True)
        expected='Axis 1 supports only' if axis==1 else 'Invalid PCB_TEST_AXIS'
        assert result.returncode!=0 and expected in result.stderr,result.stderr
    print('PASS: compile guards reject J4 PWM/motor stages and invalid axis selection.')

if __name__=='__main__': main()
