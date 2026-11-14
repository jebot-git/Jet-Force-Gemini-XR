#!/usr/bin/env python3
"""Build ROM-free Linux, Windows and standalone Quest 3 release artifacts on Linux."""
import argparse
import datetime
import hashlib
import os
from pathlib import Path
import subprocess
import sys
from package_pc import package
ROOT=Path(__file__).resolve().parents[1]
def digest(path):
    value=hashlib.sha256()
    with path.open('rb') as source:
        for chunk in iter(lambda:source.read(1048576),b''):value.update(chunk)
    return value.hexdigest()
def run(args,env=None,log=None):
    print('Running:', ' '.join(str(a) for a in args),flush=True)
    if log:
        with log.open('w') as stream:subprocess.run([str(a) for a in args],cwd=ROOT,env=env,stdout=stream,stderr=subprocess.STDOUT,check=True)
    else:subprocess.run([str(a) for a in args],cwd=ROOT,env=env,check=True)
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('target',choices=('linux','windows','quest','all'))
    parser.add_argument('--rom',type=Path,default=ROOT/'baserom.us.z64')
    parser.add_argument('--output',type=Path,default=ROOT.parent/'artifacts'/('jfg-xr-platforms-'+datetime.date.today().strftime('%Y%m%d')))
    parser.add_argument('--host-tools',type=Path,help='Directory containing a native file_to_c executable')
    parser.add_argument('--skip-prepare',action='store_true',help='Reuse already generated C/RSP code')
    parser.add_argument('--mingw',type=Path)
    parser.add_argument('--sdk',type=Path,default=Path(os.getenv('ANDROID_HOME',str(Path.home()/'Android/Sdk'))))
    parser.add_argument('--ndk',type=Path)
    parser.add_argument('--java-home',type=Path)
    parser.add_argument('--jobs',type=int,default=6)
    args=parser.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    rom=args.rom.resolve()
    if hashlib.sha1(rom.read_bytes()).hexdigest()!='493ced9008dbe932d6e91179b68e8630cf23a023':raise SystemExit('Expected the original US big-endian .z64 cartridge dump')
    deps=out/'deps';run([sys.executable,ROOT/'tools/prepare_xr_dependencies.py','--directory',deps])
    run([sys.executable,ROOT/'tools/apply_xr_patches.py'])
    env=os.environ.copy()
    private=list((ROOT/'.runtime/usr/lib/gcc').glob('*/*/libstdc++.a'))
    if private:env['LIBRARY_PATH']=str(private[0].parent)+(os.pathsep+env['LIBRARY_PATH'] if env.get('LIBRARY_PATH') else '')
    if not args.skip_prepare:
        env.update(JFG_BUILD_DIR=str(out/'host'),CMAKE_BUILD_PARALLEL_LEVEL=str(args.jobs))
        run(['bash',ROOT/'build_xr.sh',rom],env,out/'prepare.log')
    elif not (ROOT/'RecompiledFuncs/jfg_wrapped.c').is_file():raise SystemExit('Generate the game code first, or omit --skip-prepare')
    host=args.host_tools.resolve() if args.host_tools else out/'host/build-linux'
    targets=('linux','windows','quest') if args.target=='all' else (args.target,)
    releases=out/'releases';releases.mkdir(exist_ok=True)
    for target in targets:
        build=out/('build-'+target)
        command=['cmake','-S',ROOT,'-B',build,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release']
        if target=='linux':command+=['-DCMAKE_C_COMPILER=clang','-DCMAKE_CXX_COMPILER=clang++']
        else:
            if not (host/'file_to_c').is_file():raise SystemExit('Provide --host-tools with a native file_to_c executable')
            command += ['-DJFG_SDL_SOURCE='+str(deps/'SDL2'),'-DJFG_OPENXR_SDK='+str(deps/'OpenXR-SDK'),'-DJFG_HOST_FILE_TO_C='+str(host/'file_to_c')]
        if target=='windows':
            mingw=args.mingw or next(deps.glob('llvm-mingw-*-ucrt-ubuntu-*-x86_64'),None)
            if mingw is None:raise SystemExit('Provide --mingw with an extracted LLVM-MinGW UCRT toolchain; see README-BUILDS.md')
            env['JFG_LLVM_MINGW']=str(mingw.resolve())
            command+=['-DCMAKE_TOOLCHAIN_FILE='+str(ROOT/'cmake/toolchains/windows-llvm-mingw.cmake')]
        if target=='quest':
            ndk=args.ndk or args.sdk/'ndk/29.0.14206865'
            java=args.java_home or next(deps.glob('jdk-21*'),None)
            if java is None:raise SystemExit('Provide --java-home with JDK 21; see README-BUILDS.md')
            command+=['-DCMAKE_TOOLCHAIN_FILE='+str(ndk/'build/cmake/android.toolchain.cmake'),'-DANDROID_ABI=arm64-v8a','-DANDROID_PLATFORM=android-29','-DANDROID_STL=c++_shared']
        run(command,env,out/(target+'-configure.log'))
        run(['cmake','--build',build,'--target','JetForceGeminiRecompiled','--parallel',args.jobs],env,out/(target+'-build.log'))
        if target=='quest':
            version=(ROOT/'version.txt').read_text().strip()
            run([sys.executable,ROOT/'tools/package_quest.py','--build-dir',build,'--sdl-source',deps/'SDL2','--openxr-sdk',deps/'OpenXR-SDK','--sdk',args.sdk,'--ndk',ndk,'--java-home',java,'--output',releases/f'JetForceGeminiXR-{version}-quest3-arm64.apk','--keystore',out/'keys/quest-test.keystore'],env,out/'quest-package.log')
        else:package(target,build,releases,mingw if target=='windows' else None,deps)
    version=(ROOT/'version.txt').read_text().strip()
    artifacts=sorted(p for p in releases.glob('JetForceGeminiXR-'+version+'-*') if p.suffix in ('.zip','.gz','.apk'))
    (releases/'SHA256SUMS').write_text(''.join(digest(p)+'  '+p.name+'\n' for p in artifacts))
    print('Release artifacts:',releases)
if __name__=='__main__':main()
