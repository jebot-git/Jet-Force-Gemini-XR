#!/usr/bin/env python3
"""Stage ROM-free, OpenXR-only Windows and Linux bundles."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import zipfile

ROOT=Path(__file__).resolve().parents[1]
def capture(args):
    return subprocess.check_output([str(a) for a in args],text=True)
def copy(source,dest):
    dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,dest)

def package(platform,build,output,mingw=None,deps=None):
    version=(ROOT/'version.txt').read_text().strip()
    name=f'JetForceGeminiXR-{version}-{platform}-x86_64'
    stage=output/'staging'/name
    if stage.exists():shutil.rmtree(stage)
    stage.mkdir(parents=True)
    shutil.copytree(ROOT/'assets/xr',stage/'assets/xr')
    copy(ROOT/'README-XR.txt',stage/'README-XR.txt')
    copy(ROOT/'README-BUILDS.md',stage/'README-BUILDS.md')
    (stage/'roms').mkdir()
    (stage/'roms/PUT-ROM-HERE.txt').write_text('Put your unmodified US dump here as JetForceGemini.z64, or use Downloads/JetForceGemini.z64.\nNo cartridge is included. The original download name Jet Force Gemini (USA).z64 is also detected in Downloads.\n')
    for source,label in [(ROOT/'LICENSE','JFG-GPL.txt'),
        (deps/'SDL2/LICENSE.txt' if deps else Path('/nonexistent'),'SDL2.txt'),
        (deps/'OpenXR-SDK/LICENSE' if deps else Path('/nonexistent'),'OpenXR.txt'),
        (Path('/usr/share/licenses/jsoncpp/LICENSE'),'jsoncpp.txt'),
        (Path('/usr/share/licenses/gcc/COPYING3'),'GNU-GPL3.txt'),
        (Path('/usr/share/licenses/gcc/COPYING.RUNTIME'),'GCC-runtime-exception.txt')]:
        if source.is_file():copy(source,stage/'licenses'/label)
    if deps:
        shutil.copytree(deps/'OpenXR-SDK/LICENSES',stage/'licenses/OpenXR')
        for dependency in ('jsoncpp','jnipp'):
            copy(deps/'OpenXR-SDK/src/external'/dependency/'LICENSE',stage/'licenses'/(dependency+'.txt'))
    if platform=='windows':
        assert mingw
        copy(mingw/'LICENSE.TXT',stage/'licenses/LLVM-runtime.txt')
        for license in (mingw/'x86_64-w64-mingw32/share/mingw32').glob('COPYING*'):
            copy(license,stage/'licenses'/license.name)
        exe='JetForceGeminiRecompiled.exe'
        copy(build/exe,stage/exe)
        objdump=mingw/'bin/llvm-objdump'
        library_sources={p.name.lower():p for p in build.rglob('*.dll')}
        library_sources.update({p.name.lower():p for p in (mingw/'x86_64-w64-mingw32/bin').glob('*.dll')})
        checked=set();queue=[stage/exe];imports={}
        while queue:
            binary=queue.pop()
            if binary.name.lower() in checked:continue
            checked.add(binary.name.lower())
            required=re.findall(r'DLL Name:\s*(\S+)',capture([objdump,'-p',binary]))
            imports[binary.name]=required
            for dll in required:
                source=library_sources.get(dll.lower())
                if source and not (stage/source.name).exists():
                    copy(source,stage/source.name);queue.append(stage/source.name)
        assert any('openxr_loader' in n.lower() for n in imports[exe])
        assert not any(n.lower() in ('dxcompiler.dll','dxil.dll','d3d12.dll') for n in imports[exe])
        (stage/'run_xr.bat').write_bytes(b'@echo off\r\nsetlocal EnableExtensions\r\ncd /d "%~dp0"\r\nif "%~1"=="" (\r\n  JetForceGeminiRecompiled.exe\r\n  exit /b\r\n)\r\nset "FIRST=%~1"\r\nif "%FIRST:~0,2%"=="--" (JetForceGeminiRecompiled.exe %*) else (JetForceGeminiRecompiled.exe --rom "%~f1")\r\n')
        (stage/'START.txt').write_text('Windows 10/11 x86_64. Connect a PC VR headset and activate its OpenXR runtime.\nRun: run_xr.bat "C:\\path\\Jet Force Gemini (USA).z64"\nPut your US dump in Downloads/JetForceGemini.z64 or roms/JetForceGemini.z64 beside the executable.\nDouble-click run_xr.bat with no arguments. English only; no desktop game or mirror. The cartridge is not bundled.\nSaves/settings live beside the executable. No Windows HMD validation yet.\n')
        archive=output/(name+'.zip')
        with zipfile.ZipFile(archive,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=9) as z:
            for p in sorted(stage.rglob('*')):
                if p.is_file():z.write(p,(Path(name)/p.relative_to(stage)).as_posix())
        (output/'windows-imports.json').write_text(json.dumps(imports,indent=2))
    else:
        exe='JetForceGeminiRecompiled'
        copy(build/exe,stage/exe)
        subprocess.run(['strip','--strip-unneeded',str(stage/exe)],check=True)
        libs={}
        for library,path in re.findall(r'^\s*(\S+) => (/\S+) ',capture(['ldd',build/exe]),re.M):
            if library.startswith(('libSDL2','libopenxr_loader','libjsoncpp','libstdc++','libgcc_s')):
                copy(Path(path),stage/'lib'/library);libs[library]=path
        (stage/'JetForceGeminiXR.sh').write_text('#!/usr/bin/env bash\nset -euo pipefail\nHERE="$(cd "$(dirname "$0")" && pwd)"\nexport LD_LIBRARY_PATH="$HERE/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"\nexec "$HERE/JetForceGeminiRecompiled" "$@"\n')
        (stage/'JetForceGeminiXR.sh').chmod(0o755)
        versions=[]
        for p in [stage/exe]+list((stage/'lib').glob('*')):
            versions.extend(re.findall(r'GLIBC_(\d+\.\d+)',capture(['readelf','--version-info',p])))
        baseline=max(versions,key=lambda v:tuple(map(int,v.split('.'))))
        (stage/'START.txt').write_text(f'Linux x86_64, glibc {baseline} or newer. Vulkan drivers and an active OpenXR runtime are required.\nRun: ./JetForceGeminiXR.sh --rom "/path/to/Jet Force Gemini (USA).z64"\nPut your US dump in Downloads/JetForceGemini.z64 or roms/JetForceGemini.z64 beside the executable.\nRun ./JetForceGeminiXR.sh with no arguments, or pass --rom PATH.\nEnglish only; connected HMD required; no desktop game or mirror.\nThe cartridge is not bundled. Saves/settings live beside the executable.\n')
        archive=output/(name+'.tar.gz')
        with tarfile.open(archive,'w:gz') as tar:tar.add(stage,arcname=name)
        (output/'linux-runtime.json').write_text(json.dumps({'glibc_min':baseline,'libraries':libs},indent=2))
    assert not any(p.suffix.lower() in ('.z64','.n64','.v64') for p in stage.rglob('*'))
    print(archive)
    return stage,archive

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('platform',choices=('linux','windows'))
    p.add_argument('--build-dir',required=True,type=Path)
    p.add_argument('--output',required=True,type=Path)
    p.add_argument('--mingw',type=Path)
    p.add_argument('--deps',type=Path)
    args=p.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    package(args.platform,args.build_dir,args.output,args.mingw,args.deps)
if __name__=='__main__':main()
