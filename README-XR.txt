JET FORCE GEMINI — EXPERIMENTAL NATIVE OPENXR PORT

This is an initial static-recompilation prototype based on VibinToast's
JetForceGemini-Recompiled, commit f489148c060dcd34615c1055b32dd7b346e93976.
Upstream: https://github.com/VibinToast/JetForceGemini-Recompiled
The upstream GPL-3.0 license applies. Supply your own original US .z64 ROM.
ROM and generated game code are local build inputs, excluded from version control.

START
Extract the PC archive and connect the headset/controllers using an active
OpenXR runtime. On Linux run ./JetForceGeminiXR.sh; on Windows double-click
run_xr.bat. For a source checkout use ./run_xr.sh. On Quest sideload the APK
and open Jet Force Gemini XR.

Put JetForceGemini.z64 in Downloads, or in roms beside the executable.
On Quest, put it in the shared Download folder and select it once in the
file picker (which opens there). Windows/Linux detect the location automatically.
--rom-paths prints all full discovery paths without starting XR. You can pass another ROM path as
the first argument. run_xr.sh respects XR_RUNTIME_JSON; otherwise it selects
the installed WiVRn Pro runtime when available. A connected HMD and a ready native OpenXR session are mandatory. Missing
runtime/HMD or session loss causes an error/exit. There is no desktop mode,
window, mirror, display connection, keyboard/mouse handler or desktop launcher.
JFG_OPENXR=0 cannot bypass this requirement. run_linux.sh is an alias for
run_xr.sh. The executable supports --check-xr to probe HMD availability.
The game, subtitles, dialogue and settings are English only. Saved locale
choices, language overrides and locale files are ignored; no selection appears.

CONTROLS
Left stick: head-relative movement through the original game's movement code.
Left-stick click: toggle crouch; click again to stand.
Physical crouch: lower the headset at least 25 cm below its initial gameplay
height. Stand back within 15 cm of that height to release crouch. Start gameplay
in your normal standing (or seated) posture to establish the reference height.
Either physical crouching or the toggle can hold crouch. Toggle crouching also
lowers the view by up to 30 cm, accounting for any physical lowering already
present. Leaving gameplay or losing input focus clears the toggle.
Right-stick click: jump; hold for the original game's jet/fly input when the
character has that ability. Original fuel, unlocks and movement rules apply.
Jump input clears toggle crouch and overrides physical crouch while held.
Right stick: snap turning (30 degrees by default), smooth turning, or Off.
Open XR Settings with the left menu button; use the left stick up/down to
select, left/right to adjust, and the right trigger to activate. Choose Resume
or press menu again to close. The game freezes while XR Settings is open.
Turn mode, snap angle (15-90 degrees), smooth speed (15-360 degrees/second),
and right-hand weapon position/rotation are saved and apply immediately.
Quit game in XR Settings closes the native application.
Game Pause Menu in XR Settings opens or closes the original game's pause menu.
Stick/trigger inputs must be released after leaving XR Settings before they
can move, turn or fire in the game.
Legacy launch overrides remain available (menu changes can replace them):
  JFG_XR_TURN_MODE=smooth JFG_XR_TURN_SPEED=120 ./run_xr.sh
Right trigger: fire (confirm in menus).
Left trigger: original C-up action/jump (cancel in menus).
A / X: next weapon. B / Y: previous weapon.
Left menu: XR Settings. Index controllers use left B because they lack a menu button.
Touch and Pico 4 bindings are suggested; Index bindings are also included.

TRACKING AND RENDERING
The renderer replays the same game state separately for each eye, with OpenXR
asymmetric projections and predicted full head position/orientation. Each eye
renders at twice the runtime's recommended width and height (four times the pixels).
Unsupported runtime/GPU image limits produce an explicit error instead of a
silent resolution clamp. No desktop pass runs. The source rectangle is cropped before copying so a
larger cached render target cannot shrink the current frame. Periodic XR logs
report the source rectangle and swapchain dimensions.
Gameplay is first person. The native arm meshes are reposed from their wrist
attachments to the tracked controllers. Head, torso, leg and seam triangles
are excluded from the local first-person draw, without changing collision.
Arm and gun transforms are updated from the same predicted poses used by the
eye renders, including animated weapon parts. Camera history matches queued
game geometry so a newer simulation position cannot move older geometry into
the headset during locomotion/jumps. Level changes discard the previous
camera history, character pointers and tracked attachments before loading.
The gun and hand share fixed attachment-space barrel/up axes derived from
native muzzle geometry. They are not calibrated from an idle wrist pose. XR Settings provides grip-local translation
in millimetres and pitch/yaw/roll calibration. The right arm, weapon and barrel
share that calibration; the original model's muzzle offset is preserved.
HUD projection follows the output target identity across N64 framebuffer flips.
The native 4:3 HUD layout is preserved before the stereo warp, independently
of the narrow eye aspect. Reused projection records reset their UI/world flag.
Explicit shader rows are shared with a headless GPU regression check; 3D HUD
models retain internal depth ordering. The original gameplay HUD, messages, counters, dialogue windows and frames
are replayed directly into each eye at twice the runtime's recommended dimensions
(4320 x 4752 instead of 2160 x 2376 per eye on the test headset). Original cartridge glyphs, textures,
colours and layouts are kept, with the font engine's complete command stream.
There is no dialogue/HUD text decoding, substitution, custom text panel or
replacement font. The separate HUD compositor pass has been removed.
Intermediate UI/icon targets retain a high-resolution render scale equivalent
to a 4096 x 3072 native canvas. Original menus use a 4320 x 3240
subrectangle on the test headset, rendered and submitted without the former
stretch-to-eye-aspect and squeeze-back resampling. The original UI maps to a 3.6 x 2.7 metre
head-relative plane at 3.5 metres. Pause menus, the planet map and cutscenes
use the same distance. Existing XR settings remain at three metres. Damage
flashes and transition masks cover each eye rather than the HUD plane.
Level fog is disabled in XR. Depth fog, SSAO, bloom and vignette post-processing
are skipped for eye renders. Original cartridge textures retain their native
detail; this build does not include an HD texture pack.
Default world scale is 35 game units per meter; override using e.g.:
  JFG_XR_UNITS_PER_METER=40 ./run_xr.sh

BUILD AND CHECKS
Cross-platform builds and Quest ROM import: see README-BUILDS.md.
  ./build_xr.sh '/path/to/Jet Force Gemini (USA).z64'
  ./test_xr.sh
  bash test_xr_gpu.sh  # offscreen Vulkan shader test; no game or OpenXR session

Requires the upstream Linux build dependencies plus libopenxr-dev. This
machine also has a user-local Clang 21 toolchain at
~/.local/opt/jfg-toolchain/usr/lib/llvm-21/bin; build_xr.sh finds it if needed.
Desktop UI sources, RmlUi, outline-font rendering and native file dialogs
are excluded from the native executable. Only assets/xr (the bitmap font and
its license) are staged. There is no private FreeType runtime override.
The build verifies ROM SHA-1 and compares both overlay pre-linkers.
CPU checks cover native barrel alignment/muzzle transforms, stereo HUD,
menu input, original dialogue/HUD draw commands and English-only locale behavior.
The GPU check runs the actual HUD shader, continuous glyph UV calculation and
push-constant layout offscreen.

Implementation: xr/jfg_xr.cpp (OpenXR/Vulkan), xr/xr_math.h (view transforms),
src/jfg/jfg_xr_game.cpp (game camera/input/aim), xr/*.patch (renderer changes).

LIMITS / HEADSET VALIDATION
This is a prototype, not a validated playable VR release. Check stereo
orientation, world scale, locomotion, dialogue placement, weapon alignment,
pause/menu behaviour and frame pacing on the headset after each build.
The first-person rig uses the native gun attachment and sibling arm chains
identified from the model's joint-parent hierarchy. Stock-character appearance, left-arm identification,
special weapons and per-controller calibration still need headset inspection.
This is wrist-relative arm reposing, not shoulder-anchored two-bone IK. Custom
character skeletons are not guaranteed and may require separate adaptation.
Recognized rigs without an arm chain use a body-hiding fallback.
The XR pipeline uses single-sample targets at twice the recommended eye dimensions.
The earlier readable build also allocated single-sample eye targets, despite
its saved desktop MSAA2X setting. The original N64 three-point texture filter is the world-texture default;
bilinear remains an optional world-texture setting. Original 2D UI rectangles
use continuous texture coordinates and linear reconstruction of their original
texels. The legacy native-pixel grid is invalid after stereo projection and at
fractional render scales: it can drop glyph rows/columns and keep text blocky
regardless of target size. UI reconstruction bypasses that grid without changing
glyph images, font lookup, strings, layout, frames or world-sprite filtering. Nearest/linear output filtering is now honored by the native blit.
The desktop anti-aliased pixel scaler is bypassed in XR and falls back to a
linear blit. Gameplay and fitted menu rectangles copy at 1:1 pixel dimensions,
so this fallback does not rescale the original glyphs at submission. The XR
compositor may apply its own reconstruction/streaming filters. Increasing raster
resolution cannot create additional detail in the cartridge's bitmap artwork.
Existing XR settings use the bundled ASCII bitmap subset of GNU Unifont
17.0.04 (SIL OFL 1.1; assets/xr/FONT-LICENSE.txt). JFG_XR_MENU_FONT can select
another BDF font. There is no outline-font fallback.
Room-scale head movement does not move the game's collision capsule and can
look through walls. Original center-camera culling may cause peripheral
pop-in. Bosses, swimming, special weapons and scripted cameras
need further adaptation/testing. Cutscenes currently remain on a flat panel.
The original simulation rate remains; stereo rendering does not double game
updates. Rendering both eyes uses synchronous Vulkan copies; frame pacing still needs
headset testing.
The runtime and renderer must select the same GPU; a mismatch exits
with an error in stderr. The native targets are Windows x86_64, Linux x86_64 and Quest 3 ARM64.
See README-BUILDS.md for packaging, requirements and validation limits.

For a later headset diagnostic, capture stderr:
  ./run_xr.sh 2>&1 | tee /tmp/jfg-xr-session.log
Expected milestones: [XR] instance ready, [XR] Vulkan session ready,
then periodic [XR] frames=... entries. These are not evidence of visual
correctness; inspect the headset view and test the controls directly.

BUILD VERIFICATION — 2026-11-14
Optimized Release builds are packaged for Linux x86_64, Windows x86_64 and
Quest 3/3S ARM64. Linux headset rendering and HUD/dialogue readability passed
user validation after correcting native-pixel UV snapping on original UI.
CPU tests cover tracking, native UI commands, English-only behavior, ROM
location precedence and Unicode filenames. The Vulkan regression checks the
actual HUD projection and texture-coordinate helper on Intel Arc A770.
Windows ROM discovery and CPU/UI tests pass under Wine; missing-HMD and
unsupported desktop modes are rejected. Quest APK signatures and 16 KiB
alignment pass. Windows and Quest headset rendering/performance remain
unverified. The packages include no cartridge dump or private signing key.
