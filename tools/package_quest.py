#!/usr/bin/env python3
"""Package the native ARM64 OpenXR game as a locally signed Quest test APK."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import zipfile
import xml.etree.ElementTree as ET

ROOT=Path(__file__).resolve().parents[1]
def run(args,env=None):
    subprocess.run([str(a) for a in args],check=True,env=env)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir",type=Path,required=True)
    parser.add_argument("--sdl-source",type=Path,required=True)
    parser.add_argument("--openxr-sdk",type=Path,required=True)
    parser.add_argument("--sdk",type=Path,default=Path(os.getenv("ANDROID_HOME",str(Path.home()/"Android/Sdk"))))
    parser.add_argument("--ndk",type=Path,required=True)
    parser.add_argument("--java-home",type=Path,required=True)
    parser.add_argument("--output",type=Path,required=True)
    parser.add_argument("--keystore",type=Path,required=True)
    args=parser.parse_args()
    args.output=args.output.resolve();args.output.parent.mkdir(parents=True,exist_ok=True)
    env=os.environ.copy();env["JAVA_HOME"]=str(args.java_home)
    env["PATH"]=str(args.java_home/"bin")+os.pathsep+env.get("PATH","")
    tools=args.sdk/"build-tools"/"35.0.0"
    android_jar=args.sdk/"platforms/android-34/android.jar"
    native=args.ndk/"toolchains/llvm/prebuilt/linux-x86_64"
    with tempfile.TemporaryDirectory(prefix="jfg-quest-",dir=args.output.parent) as tmp:
        stage=Path(tmp);assets=stage/"assets";res=stage/"res/drawable"
        (assets/"licenses").mkdir(parents=True);res.mkdir(parents=True)
        shutil.copytree(ROOT/"assets/xr",assets/"xr")
        shutil.copy2(ROOT/"icons/512.png",res/"icon.png")
        for source,name in [(ROOT/"LICENSE","JFG-GPL.txt"),(args.sdl_source/"LICENSE.txt","SDL2.txt"),
            (args.openxr_sdk/"LICENSE","OpenXR.txt"),(args.ndk/"NOTICE","Android-NDK-NOTICE.txt"),
            (args.ndk/"NOTICE.toolchain","Android-toolchain-NOTICE.txt")]:
            shutil.copy2(source,assets/"licenses"/name)
        shutil.copytree(args.openxr_sdk/"LICENSES",assets/"licenses/OpenXR")
        for dependency in ("jsoncpp","jnipp"):
            shutil.copy2(args.openxr_sdk/"src/external"/dependency/"LICENSE",assets/"licenses"/(dependency+".txt"))
        manifest=ET.parse(ROOT/"android/AndroidManifest.xml")
        ns="{http://schemas.android.com/apk/res/android}"
        version=(ROOT/"version.txt").read_text().strip()
        major,minor,patch=map(int,version.split("."))
        manifest.getroot().set(ns+"versionName",version)
        manifest.getroot().set(ns+"versionCode",str(major*10000+minor*100+patch))
        manifest.getroot().find("application").set(ns+"icon","@drawable/icon")
        manifest_path=stage/"AndroidManifest.xml";manifest.write(manifest_path,encoding="utf-8",xml_declaration=True)
        unsigned=stage/"unsigned.apk"
        resources=stage/"resources.zip"
        run([tools/"aapt2","compile","--dir",stage/"res","-o",resources],env)
        run([tools/"aapt2","link","-I",android_jar,"--manifest",manifest_path,"-A",assets,"-o",unsigned,resources],env)
        sources=list((args.sdl_source/"android-project/app/src/main/java").rglob("*.java"))
        sources+=list((ROOT/"android/src").rglob("*.java"))
        classes=stage/"classes";classes.mkdir()
        source_list=stage/"sources.txt";source_list.write_text("\n".join(str(p) for p in sources))
        run([args.java_home/"bin/javac","--release","8","-classpath",android_jar,"-d",classes,"@"+str(source_list)],env)
        dex=stage/"dex";dex.mkdir()
        run([tools/"d8","--min-api","29","--lib",android_jar,"--output",dex]+list(classes.rglob("*.class")),env)
        libs=stage/"lib/arm64-v8a";libs.mkdir(parents=True)
        for name,source in {
            "libJetForceGeminiRecompiled.so":args.build_dir/"libJetForceGeminiRecompiled.so",
            "libSDL2.so":args.build_dir/"libSDL2.so",
            "libopenxr_loader.so":args.build_dir/"libopenxr_loader.so",
            "libc++_shared.so":native/"sysroot/usr/lib/aarch64-linux-android/libc++_shared.so",
        }.items():
            shutil.copy2(source,libs/name)
            run([native/"bin/llvm-strip","--strip-unneeded",libs/name],env)
        with zipfile.ZipFile(unsigned,"a",compression=zipfile.ZIP_DEFLATED) as apk:
            for p in sorted(libs.glob("*.so")):apk.write(p,p.relative_to(stage).as_posix())
            for p in dex.glob("*.dex"):apk.write(p,p.name)
        aligned=stage/"aligned.apk"
        run([tools/"zipalign","-P","16","-f","4",unsigned,aligned],env)
        if not args.keystore.exists():
            args.keystore.parent.mkdir(parents=True,exist_ok=True)
            run([args.java_home/"bin/keytool","-genkeypair","-noprompt","-keystore",args.keystore,
                "-alias","jfgxr-test","-keyalg","RSA","-keysize","2048","-validity","10000",
                "-storepass","android","-keypass","android","-dname","CN=Jet Force Gemini XR Local Test"],env)
            args.keystore.chmod(0o600)
        run([tools/"apksigner","sign","--ks",args.keystore,"--ks-key-alias","jfgxr-test",
            "--ks-pass","pass:android","--key-pass","pass:android","--v1-signing-enabled","true",
            "--v2-signing-enabled","true","--v3-signing-enabled","false",
            "--v4-signing-enabled","false","--out",args.output,aligned],env)
        run([tools/"apksigner","verify","--verbose",args.output],env)
        run([tools/"apksigner","verify","--verbose","--min-sdk-version","21",args.output],env)
        run([tools/"zipalign","-c","-P","16","4",args.output],env)
        with zipfile.ZipFile(args.output) as apk:
            names=apk.namelist()
            assert "classes.dex" in names
            assert all("lib/arm64-v8a/"+p.name in names for p in libs.glob("*.so"))
            assert not any(n.lower().endswith((".z64",".n64",".v64")) for n in names)
        Path(str(args.output)+".idsig").unlink(missing_ok=True)
        args.output.chmod(0o644)
        print("Quest APK:",args.output)
if __name__=="__main__":main()
