#!/usr/bin/env python3
"""Apply the native XR patches without resetting local commits or edits."""
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[1]
PATCHES=[
 ('lib/rt64','12b3c8525ffde478e423e72117de9ccbe5a3a5b1','rt64.patch'),
 ('lib/rt64/src/contrib/plume','429523410391564410bca3f806e74a858525e49a','plume.patch'),
 ('lib/N64ModernRuntime','3e612160d99a4833c74213724377feabf9513ac6','runtime.patch'),
]
def main():
    status=subprocess.check_output(['git','submodule','status','--recursive'],cwd=ROOT,text=True)
    divergent=[line for line in status.splitlines() if line.startswith(('+','U'))]
    if divergent:raise SystemExit('Submodule commits differ from the recorded snapshots; no checkout was reset:\n'+'\n'.join(divergent))
    subprocess.run(['git','submodule','update','--init','--recursive'],cwd=ROOT,check=True)
    for relative,revision,name in PATCHES:
        path=ROOT/relative;patch=ROOT/'xr'/name
        head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=path,text=True).strip()
        if head!=revision:raise SystemExit(f'{relative} is at {head}; expected {revision}. Local commits were preserved.')
        command=['git','apply']
        if subprocess.run(command+['--reverse','--check',str(patch)],cwd=path,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL).returncode==0:continue
        subprocess.run(command+['--check',str(patch)],cwd=path,check=True)
        subprocess.run(command+[str(patch)],cwd=path,check=True)
if __name__=='__main__':main()
