#!/usr/bin/env python3
"""Fetch the pinned, public SDL2 and Khronos OpenXR sources."""
import argparse
from pathlib import Path
import subprocess
SOURCES=[
 ('SDL2','https://github.com/libsdl-org/SDL.git','release-2.32.10','5d249570393f7a37e037abf22cd6012a4cc56a71'),
 ('OpenXR-SDK','https://github.com/KhronosGroup/OpenXR-SDK.git','release-1.1.65','24a1f2b92d212501a2b1b8d30ffd53abf55fcfa8'),
]
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory',required=True,type=Path)
    root=parser.parse_args().directory.resolve();root.mkdir(parents=True,exist_ok=True)
    for name,url,tag,revision in SOURCES:
        path=root/name
        if not path.exists():subprocess.run(['git','clone','--depth','1','--branch',tag,url,str(path)],check=True)
        head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=path,text=True).strip()
        if head!=revision:raise SystemExit(f'{path} is at {head}; expected {revision}. Existing checkout was preserved.')
        print(name,head)
if __name__=='__main__':main()
