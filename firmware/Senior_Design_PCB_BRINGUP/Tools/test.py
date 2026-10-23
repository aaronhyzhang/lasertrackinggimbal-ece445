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
    cases=[('test_motor',3,0),('test_driver',3,0),('test_driver',3,1)]
    cases += [('test_app',s,0) for s in range(4)] + [('test_app',3,1)]
    for name,stage,bench in cases:
        binary=out/(f'{name}_{stage}_{bench}'+('.exe' if sys.platform=='win32' else ''))
        flags=['-std=c11','-Wall','-Wextra','-Werror',f'-DPCB_STAGE={stage}',
               f'-DPCB_BENCH_ALLOW_CPUV_HISTORY={bench}']
        sources=[root/f'Tests/{name}.c']
        if name=='test_app':
            flags+=['-I',str(root/'Tests/fakes')]
            sources+=[root/'Core/Src/demo_app.c']
        flags+=['-I',str(root/'Core/Inc')]
        sources+=([root/'Core/Src/drv8304.c'] if name=='test_driver' else
                  [root/'Core/Src/open_loop.c',root/'Core/Src/modulation.c'])
        subprocess.run([*cc,*flags,*map(str,sources),'-lm','-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)

if __name__=='__main__': main()
