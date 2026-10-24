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
    cases=[]
    for axis in range(2):
        cases += [('test_motor',3,0,axis),('test_driver',3,0,axis),('test_driver',3,1,axis)]
        cases += [('test_app',s,0,axis) for s in range(4)]
        cases += [('test_app',3,1,axis),('test_axis',3,0,axis)]
    cases=[(*case,15) for case in cases]
    cases += [('test_driver',3,bench,axis,45) for axis in range(2) for bench in range(2)]
    for name,stage,bench,axis,source_ma in cases:
        binary=out/(f'{name}_{stage}_{bench}_{axis}_{source_ma}'+('.exe' if sys.platform=='win32' else ''))
        flags=['-std=c11','-Wall','-Wextra','-Werror',f'-DPCB_STAGE={stage}',
               f'-DPCB_BENCH_ALLOW_CPUV_HISTORY={bench}',f'-DPCB_TEST_AXIS={axis}',
               '-DPCB_BENCH_CAPTURE_DRIVER_FAULT=0',f'-DPCB_HS_SOURCE_MA={source_ma}']
        sources=[root/f'Tests/{name}.c']
        if name=='test_app':
            flags+=['-I',str(root/'Tests/fakes')]
            sources+=[root/'Core/Src/demo_app.c']
        flags+=['-I',str(root/'Core/Inc')]
        if name=='test_driver': sources+=[root/'Core/Src/drv8304.c']
        elif name!='test_axis': sources+=[root/'Core/Src/open_loop.c',root/'Core/Src/modulation.c']
        subprocess.run([*cc,*flags,*map(str,sources),'-lm','-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)
    for stage,axis in [(-1,0),(4,1),(2,2),(3,-1)]:
        result=subprocess.run([*cc,'-std=c11','-fsyntax-only','-x','c',
            '-I',str(root/'Core/Inc'),f'-DPCB_STAGE={stage}',f'-DPCB_TEST_AXIS={axis}','-'],
            input='#include "pcb_config.h"\n',text=True,capture_output=True)
        expected='Invalid PCB_STAGE' if stage not in range(4) else 'Invalid PCB_TEST_AXIS'
        assert result.returncode!=0 and expected in result.stderr,result.stderr
    for stage,capture in [(0,1),(1,1),(2,1),(3,2)]:
        result=subprocess.run([*cc,'-std=c11','-fsyntax-only','-x','c',
            '-I',str(root/'Core/Inc'),f'-DPCB_STAGE={stage}',
            f'-DPCB_BENCH_CAPTURE_DRIVER_FAULT={capture}','-'],
            input='#include "pcb_config.h"\n',text=True,capture_output=True)
        expected='Fault capture requires' if capture==1 else 'Invalid PCB_BENCH_CAPTURE_DRIVER_FAULT'
        assert result.returncode!=0 and expected in result.stderr,result.stderr
    for source_ma in [0,30]:
        result=subprocess.run([*cc,'-std=c11','-fsyntax-only','-x','c',
            '-I',str(root/'Core/Inc'),f'-DPCB_HS_SOURCE_MA={source_ma}','-'],
            input='#include "pcb_config.h"\n',text=True,capture_output=True)
        assert result.returncode!=0 and 'Invalid PCB_HS_SOURCE_MA' in result.stderr,result.stderr
    print('PASS: 10 compile guards reject invalid stage/axis/capture/source settings; both axes support all stages with capture off.')

if __name__=='__main__': main()
