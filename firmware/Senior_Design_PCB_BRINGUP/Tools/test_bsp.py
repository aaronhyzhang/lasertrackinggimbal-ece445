"""Windows x64 audit of actual board.c + vendor GPIO HAL on private simulated MMIO."""
import argparse
import pathlib
import subprocess
import sys

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc',default='cc')
    parser.add_argument('--zig',type=pathlib.Path)
    parser.add_argument('--capture',type=int,choices=[0,1],default=0)
    args=parser.parse_args()
    if sys.platform!='win32':
        parser.error('This optional register audit requires Windows x64; portable core tests use Tools/test.py.')
    root=pathlib.Path(__file__).resolve().parents[1]
    out=root/'build/host-tests';out.mkdir(parents=True,exist_ok=True)
    cc=[str(args.zig),'cc'] if args.zig else [args.cc]
    flags=['-std=c11','-O1','-UNDEBUG','-Wall','-Wextra','-Werror','-Wno-unused-parameter',
           '-Wno-pointer-to-int-cast','-Wno-int-to-pointer-cast',
           '-DUSE_HAL_DRIVER','-DSTM32H743xx','-DPCB_STAGE=3','-DPCB_TEST_AXIS=1',
           f'-DPCB_BENCH_CAPTURE_DRIVER_FAULT={args.capture}']
    for include in ['Core/Inc','Core/Src','Drivers/STM32H7xx_HAL_Driver/Inc',
                    'Drivers/CMSIS/Include','Drivers/CMSIS/Device/ST/STM32H7xx/Include']:
        flags+=['-I',str(root/include)]
    sources=[root/'Tests/test_bsp.c',root/'Tests/bsp_unused_hal.c',
             root/'Core/Src/drv8304.c',root/'Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_gpio.c']
    binary=out/f'test_bsp_capture{args.capture}.exe'
    subprocess.run([*cc,*flags,*map(str,sources),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)

if __name__=='__main__':main()
