# Native OpenXR builds

The supported build targets are Windows 10/11 x86_64, Linux x86_64, and
standalone Quest 3/3S ARM64. All use Vulkan/OpenXR, English text and bitmap UI.
There is no desktop game renderer or mirror. A working XR HMD is mandatory.

The cartridge is never included in the release archives or APK. Use your own
unmodified US big-endian `.z64` dump (SHA-1
`493ced9008dbe932d6e91179b68e8630cf23a023`). Generated game code stays local.

## Build from Linux

Install the native build dependencies described in `build_linux.sh`, including
Clang, CMake, Ninja, Python, SDL2 and OpenXR development headers/libraries.
Cross builds also need:

- LLVM-MinGW UCRT for Windows. Tested:
  [20261104 Linux x86_64 toolchain](https://github.com/mstorsjo/llvm-mingw/releases/tag/20261104),
  archive `llvm-mingw-20261104-ucrt-ubuntu-22.04-x86_64.tar.xz`, SHA-256
  `919380403de87dca3c581c2053230cd298af5e891286628ba0613b7fed370926`.
- Android SDK with build-tools 35.0.0, platform android-34, and NDK
  29.0.14206865. These may live under `~/Android/Sdk` or `ANDROID_HOME`.
- JDK 21. Tested: Eclipse Temurin 21.0.13+9, Linux x64 archive SHA-256
  `d7f5d5cf865a2902b70ff549b97586a7d3f234e016f041ab447ea740c7d66d1c`.

The builder fetches pinned SDL2 2.32.10 and Khronos OpenXR-SDK 1.1.65 source
checkouts. Compiler/JDK downloads are supplied separately. Existing dependency
commits and local changes are preserved; mismatched snapshots stop the build.

```sh
python3 tools/build_platforms.py all \
  --rom '/path/to/Jet Force Gemini (USA).z64' \
  --output '/path/to/build-output' \
  --mingw '/path/to/llvm-mingw-20261104-ucrt-ubuntu-22.04-x86_64' \
  --java-home '/path/to/jdk-21.0.13+9'
```

Use `linux`, `windows` or `quest` instead of `all` for one target. The default
preparation builds the native tools and generates C/RSP code from the dump.
For an existing generated checkout, use `--skip-prepare --host-tools DIR`,
where DIR contains the native `file_to_c` executable. `--sdk`, `--ndk` and
`--jobs` override the Android paths and compiler parallelism.

The builder produces optimized Release artifacts, keeps unstripped build
outputs for diagnostics, and writes archives plus `SHA256SUMS` under
`build-output/releases`. Native Windows development can also use the updated
`build_windows.bat` with its documented Visual Studio/Clang prerequisites.

## Run

Put your own US `.z64` dump in an easy-to-reach location:

| Platform | ROM location | Launch |
| --- | --- | --- |
| Windows | `Downloads\JetForceGemini.z64`, or `roms\JetForceGemini.z64` beside the executable | Double-click `run_xr.bat` |
| Linux | `~/Downloads/JetForceGemini.z64`, or `roms/JetForceGemini.z64` beside the executable | Run `./JetForceGeminiXR.sh` |
| Quest 3/3S | Shared `Download/JetForceGemini.z64` on the headset | Open the app and select the file once; the picker starts in Downloads |

Windows and Linux also recognize `Jet Force Gemini (USA).z64` and
`baserom.us.z64` in Downloads. The portable `roms` folder takes priority over
Downloads. Windows uses the system Downloads folder; Linux honors
`XDG_DOWNLOAD_DIR`. A legacy `baserom.us.z64` beside the executable still works.
`--rom PATH` overrides discovery, followed by the `JFG_ROM` environment variable.
`--rom-paths` lists full paths and the selected dump without starting XR.
Previously imported dumps continue to work after the original file is moved.
The game validates the cartridge contents; changing a filename cannot make a
wrong or modified dump valid.

**Linux:** the current release was built on Nobara/Fedora 44 and requires
**glibc 2.43 or newer**. It bundles SDL2, the OpenXR loader and C++ runtime
libraries. Vulkan drivers and an active HMD runtime remain system components.
`XR_RUNTIME_JSON` may select a runtime explicitly.

**Windows:** Windows 10/11 x86_64, a connected PC VR headset and an active
OpenXR runtime are required. The zip includes the OpenXR loader and LLVM C++
runtime DLLs. Saves and settings live beside the game.

**Quest:** sideload the APK, then copy your dump into the headset's Downloads
folder using USB file transfer or ADB:

```sh
adb install -r JetForceGeminiXR-1.3.3-quest3-arm64.apk
adb push '/path/to/Jet Force Gemini (USA).z64' /sdcard/Download/JetForceGemini.z64
```

The importer validates the file and copies it to private app storage. Gameplay
and saves then run entirely on the headset, with no WiVRn/streaming PC required.
Removing the app deletes its private saves. The APK is signed with a local test
key, kept outside the repository in `build-output/keys`; retain that key for
future updates. No deployment or store publication is performed by the builder.

## Validation

- `bash test_xr.sh`: stereo math, hand/weapon transforms, arm hierarchy,
  controller menu edges, portable/Downloads ROM discovery and Unicode paths,
  original dialogue/HUD glyph and frame commands, and
  English-only behavior.
- `bash test_xr_gpu.sh`: actual HUD shader and push-constant layout on Vulkan.
- Windows CLI/HMD-required startup and native math tests were run under Wine.
- Quest native libraries are ARM64 and the APK is checked for v1/v2 signatures,
  complete JNI/library packaging and 16 KiB ELF/ZIP alignment.

Windows and Quest headset rendering, input and performance still require
on-device validation. A successful build/signature check does not establish
playability. No ADB device was connected during this build.

Android startup follows the [Khronos Android loader initialization](https://registry.khronos.org/OpenXR/specs/1.0/html/xrspec.html#XR_KHR_loader_init_android)
and [Meta OpenXR instance/session requirements](https://developers.meta.com/vr/documentation/native/android/mobile-openxr-instance-session/).
