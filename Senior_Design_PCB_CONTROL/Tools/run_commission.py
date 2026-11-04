"""Launch commissioning in a local environment; first use may install requirements.

Examples: py -3 Tools/run_commission.py demo
          py -3 Tools/run_commission.py info
"""
import importlib.util
from pathlib import Path
import subprocess
import sys


def main():
    root=Path(__file__).resolve().parents[1]
    if all(importlib.util.find_spec(name) for name in ('numpy','scipy','matplotlib')):
        python=Path(sys.executable)
    else:
        shared=root.parent/'Senior_Design_FW/tmp/tuning_venv/Scripts/python.exe'
        local=root/'build/commission-venv/Scripts/python.exe'
        python=shared if shared.exists() else local
        if not python.exists():
            subprocess.run([sys.executable,'-m','venv',str(root/'build/commission-venv')],check=True)
            subprocess.run([str(python),'-m','pip','install','-r',str(root/'Tools/requirements-tuning.txt')],check=True)
    raise SystemExit(subprocess.call([str(python),str(root/'Tools/commission.py'),*sys.argv[1:]]))


if __name__=='__main__':main()
