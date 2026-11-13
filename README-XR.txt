JET FORCE GEMINI — EXPERIMENTAL LINUX OPENXR PORT

This is an initial static-recompilation prototype based on VibinToast's
JetForceGemini-Recompiled, commit f489148c060dcd34615c1055b32dd7b346e93976.
Upstream: https://github.com/VibinToast/JetForceGemini-Recompiled
The upstream GPL-3.0 license applies. Supply your own original US .z64 ROM.
ROM and generated game code are local build inputs, excluded from version control.

START
Connect your headset and controllers to WiVRn Pro, then run:

  cd /home/blux/de-xr/jet-force-gemini-xr
  ./run_xr.sh

The local baserom.us.z64 is used by default. You can pass another ROM path as
the first argument. run_xr.sh respects XR_RUNTIME_JSON; otherwise it selects
the installed WiVRn Pro runtime when available. Runtime initialization may
wait for the headset to connect. Ctrl+C stops the launch.

For ordinary desktop mode:
  JFG_OPENXR=0 bash run_linux.sh

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
Right stick: 30-degree snap turn; return stick to center between turns.
Optional smooth turning (90 degrees/second at full stick):
  JFG_XR_TURN_MODE=smooth ./run_xr.sh
Adjust maximum speed in degrees/second (15–360):
  JFG_XR_TURN_MODE=smooth JFG_XR_TURN_SPEED=120 ./run_xr.sh
Unset JFG_XR_TURN_MODE or set it to snap to restore snap turning. Smooth turn
uses a stick deadzone and proportional speed, runs only during focused gameplay,
and limits elapsed time after stalls. These settings are read at launch.
Right trigger: fire (confirm in menus).
Left trigger: original C-up action/jump (cancel in menus).
A / X: next weapon. B / Y: previous weapon.
Left menu: pause. Index controllers use left B because they lack a menu button.
Touch and Pico 4 bindings are suggested; Index bindings are also included.

TRACKING AND RENDERING
The renderer replays the same game state separately for each eye, with OpenXR
asymmetric projections and predicted full head position/orientation. It keeps
a desktop view too. Gameplay camera is first person and the local character
mesh is hidden. Menus/cutscenes use a floating flat panel. Both controllers'
grip and aim poses are sampled; colored grip markers stand in for hand meshes.
Right-hand aim drives the original weapon aim fields and barrel origin.
Default world scale is 35 game units per meter; override using e.g.:
  JFG_XR_UNITS_PER_METER=40 ./run_xr.sh

BUILD AND CHECKS
  ./build_xr.sh '/path/to/Jet Force Gemini (USA).z64'
  ./test_xr.sh

Requires the upstream Linux build dependencies plus libopenxr-dev. This
machine also has a user-local Clang 21 toolchain at
~/.local/opt/jfg-toolchain/usr/lib/llvm-21/bin; build_xr.sh finds it if needed.
This desktop's system FreeType is a custom bitmap-only build. An unmodified
Ubuntu libfreetype6 2.14.2+dfsg-1ubuntu0.2 package has been extracted under
.runtime/ for the game. xr/launch_environment.sh selects this local library;
the desktop's libraries are unchanged. On another standard Linux installation
this override should not be necessary. .runtime/ is excluded from git.
The build wrapper applies the included RT64/Plume patches to pinned submodules
and preserves them during the upstream build. Default build parallelism is 3.
The build verifies ROM SHA-1 and compares both overlay pre-linkers.

Implementation: xr/jfg_xr.cpp (OpenXR/Vulkan), xr/xr_math.h (view transforms),
src/jfg/jfg_xr_game.cpp (game camera/input/aim), xr/*.patch (renderer changes).

LIMITS / HEADSET TEST STILL REQUIRED
This is a prototype, not a validated playable VR release. The user deferred
live headset testing. Check stereo orientation, world scale, locomotion,
weapon origin/direction, pause/menu behavior and frame pacing on the headset.
Hand markers are simple quads, not articulated hands or held weapon models.
Room-scale head movement does not move the game's collision capsule and can
look through walls. Original center-camera culling may cause peripheral
pop-in. Bosses, swimming, special weapons, HUD depth and scripted cameras
need further adaptation/testing. Snap turning and locomotion are unvalidated.
The original simulation rate remains; stereo rendering does not double game
updates. Rendering two eyes plus the desktop with synchronous Vulkan copies
is intentionally basic and will need performance work.
The runtime and renderer must select the same GPU; a mismatch disables XR
with an error in stderr. The game currently targets Linux/Vulkan only.

For a later headset diagnostic, capture stderr:
  ./run_xr.sh 2>&1 | tee /tmp/jfg-xr-session.log
Expected milestones: [XR] instance ready, [XR] Vulkan session ready,
then periodic [XR] frames=... entries. These are not evidence of visual
correctness; inspect the headset view and test the controls directly.

BUILD VERIFICATION — 2026-11-13
The final Linux executable linked successfully with libopenxr_loader.so.1.
Overlay pre-link comparison passed: 155 overlays, 60,981 changed bytes agree.
Math tests passed: stereo IPD, forward convention, positional tracking, roll,
and stick deadzone. Patch reapplication checks and shell syntax checks passed.
A timed desktop startup accepted the ROM, loaded fonts, initialized RT64
(setup result 0, Vulkan), and reached game initialization. The process was
stopped by the test timeout; this is not a playthrough or visual validation.
Live OpenXR/headset checks were explicitly deferred by the user.
Build log: /tmp/jfg-build-verified.log
Desktop startup log: /tmp/jfg-desktop-verified.log
