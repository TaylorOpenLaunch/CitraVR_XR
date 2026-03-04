# Android XR Port Notes (Emulator First)

## Scope and branch policy
- Fork: `https://github.com/TaylorOpenLaunch/CitraVR_XR`
- Upstream (read-only reference): `https://github.com/amwatson/CitraVR`
- Working branch: `androidxr-experiment`
- Policy followed: no pushes/merges to any `main` branch.

## Baseline repo/build status
- Submodules initialized successfully.
- Baseline Android build succeeded before XR-specific fixes.
- Build command used:
  - `cd src/android`
  - `PATH=/tmp/citravr_glslang/StandAlone:$PATH ./gradlew :app:assembleCanaryDebug`
- Host requirement discovered during bring-up:
  - `glslangValidator` needed on PATH (used `/tmp/citravr_glslang/StandAlone/glslangValidator`).

## Android/Gradle toolchain discovered
- AGP: `8.2.1` (`src/android/build.gradle.kts`)
- Kotlin plugin: `1.9.22`
- Gradle wrapper: `8.2` (`src/android/gradle/wrapper/gradle-wrapper.properties`)
- compileSdk: `android-34`
- NDK: `26.1.10909125`
- Java/Kotlin target: `17`

## Emulator setup used
- AVD: `XR_Headset`
- AVD image: `system-images/android-34/google-xr/arm64-v8a/`
- Tag: `android-xr` (Google Play)
- Device model: `Android XR SDK built for arm64`
- Fingerprint: `google/gms_sdk_xr64_arm64/emulator64_arm64:14/UP1A.231005.007.A1/13953962:userdebug/test-keys`
- Key AVD config:
  - `hw.lcd.width=2560`
  - `hw.lcd.height=2558`
  - `hw.ramSize=4096`
  - `abi.type=arm64-v8a`
- OpenXR-related packages confirmed on device:
  - `com.android.openxr.broker`
  - `com.android.openxrsample`
  - `com.android.xranchormanager`

## Launch/run commands used
- Install:
  - `adb -s emulator-5554 install -r src/android/app/build/outputs/apk/canary/debug/app-canary-debug.apk`
- Start VR activity explicitly (important; launcher often opens 2D `MainActivity`):
  - `adb -s emulator-5554 shell am start -W -n org.citra.citra_emu.playtest.debug/org.citra.citra_emu.vr.VrActivity -a android.intent.action.MAIN -c org.khronos.openxr.intent.category.IMMERSIVE_HMD`
- Log capture filters used:
  - `CITRAVR_XR`
  - `CITRAVR_PORT`
  - `xrCreateSession|xrBeginSession|Frame[`

## Code changes made (emulator bring-up focused)

### 1) OpenXR diagnostics and capability logging
Files:
- `src/android/app/src/main/jni/vr/utils/LogUtils.h`
- `src/android/app/src/main/jni/vr/OpenXR.h`
- `src/android/app/src/main/jni/vr/OpenXR.cpp`

What was added:
- Dedicated tags/macros: `CITRAVR_XR`, `CITRAVR_PORT`.
- Enumerate+print full `xrEnumerateInstanceExtensionProperties` list.
- Log symbolic + numeric results for key calls, including:
  - `xrCreateInstance`
  - `xrGetSystem`
  - `xrGetSystemProperties`
  - `xrCreateSession`
  - swapchain format enumeration
  - `xrCreateSwapchainAndroidSurfaceKHR`
- Track enabled instance extensions at runtime and query with `OpenXrIsExtensionEnabled`.

### 2) Compatibility gating and Quest-specific optional features
Files:
- `src/android/app/src/main/jni/vr/vr_main.cpp`
- `src/android/app/src/main/jni/vr/layers/GameSurfaceLayer.cpp`
- `src/android/app/src/main/jni/vr/layers/UILayer.cpp`
- `src/android/app/src/main/jni/vr/XrController.cpp`

What was changed:
- Optionalized extension-dependent paths (`XR_FB_passthrough`, performance metrics, android thread settings).
- Cylinder layer only used when extension exists; fallback to quad path.
- Oculus interaction profile suggestion no longer hard-fails startup.
- Input polling now handles unsupported paths safely for bring-up.
- Fixed input bug causing `XR_ERROR_PATH_UNSUPPORTED`:
  - left/right index actions were queried with subaction paths even though created without them; now queried with `XR_NULL_PATH`.
- Added first-frame diagnostics:
  - logs for `xrWaitFrame`, `xrBeginFrame`, `xrEndFrame` for first 5 frames.
- Relaxed one destructor assert to warning to avoid abort during runtime-driven session exit.

### 3) Android manifest adjustments (minimal)
File:
- `src/android/app/src/main/AndroidManifest.xml`

Changes:
- `com.oculus.feature.PASSTHROUGH` set to `required="false"`.
- Added OpenXR permission declarations (`OPENXR`, `OPENXR_SYSTEM`).
- Added `android.software.xr.api.openxr` feature (non-required).
- `VrActivity` intent supports `org.khronos.openxr.intent.category.IMMERSIVE_HMD`.
- Added XR activity start mode property:
  - `XR_ACTIVITY_START_MODE_FULL_SPACE_UNMANAGED`.

## OpenXR capability summary from emulator runtime
From diagnostics:
- Runtime name/version: `Android XR` / `1.0.0`
- System: `Moohan`, vendor `1256`
- Max swapchain size: `16384x16384`
- Max layers: `16`
- Extensions enumerated: `68`
- App-enabled extension set (current bring-up path):
  - `XR_KHR_opengl_es_enable`
  - `XR_KHR_android_surface_swapchain`
  - `XR_KHR_android_create_instance`
  - `XR_KHR_composition_layer_cylinder`
  - `XR_EXT_performance_settings`
  - `XR_KHR_android_thread_settings`
  - `XR_KHR_composition_layer_equirect2`

## Minimal rendering validation (success criteria)
- `[x]` App launches on Android XR emulator (`VrActivity`)
- `[x]` OpenXR session created (`xrCreateSession => XR_SUCCESS`)
- `[x]` Swapchain created (`xrCreateSwapchainAndroidSurfaceKHR => XR_SUCCESS`)
- `[x]` At least one frame rendered/submitted (`Frame[1] xrEndFrame layerCount=4`)

Representative evidence from run log:
- `CITRAVR_XR: xrCreateSession => 0 (XR_SUCCESS)`
- `CITRAVR_XR: xrCreateSwapchainAndroidSurfaceKHR => 0 (XR_SUCCESS)`
- `CITRAVR_XR: Frame[1] xrWaitFrame ...`
- `CITRAVR_XR: Frame[1] xrBeginFrame`
- `CITRAVR_XR: Frame[1] xrEndFrame layerCount=4`
- `CITRAVR_XR: Frame[5] xrEndFrame layerCount=4`

## Remaining gaps vs Quest runtime (non-goals for this phase)
- Passthrough and Meta-only metrics extensions are unavailable and gated off.
- Controller/input behavior is still not parity-validated; bring-up prioritizes frame loop.
- Frequent runtime-side compositor warnings remain:
  - `Failed to latch buffer in OpenXRHandler: main`
  - Indicates swapchain image/latch behavior mismatch still exists.
- Session lifecycle on emulator tends to transition to exiting quickly; process ends cleanly afterward.

## Blockers / risks for next phase
- Need a controlled static render path (solid color or test quad) that does not depend on app UI surface timing to reduce latch-buffer failures.
- Need explicit handling of runtime-driven session transitions (VISIBLE/SYNCHRONIZED/STOPPING/EXITING) without relying on destructor-time warnings.
- Input mapping should be revisited for Android XR interaction profiles instead of Oculus-only assumptions.

## Headset Phase Checklist (do not execute yet)
1. Confirm device OpenXR runtime package and version (`xrGetInstanceProperties` in logs).
2. Re-run extension dump and compare against emulator list.
3. Verify required set still present:
   - `XR_KHR_opengl_es_enable`
   - `XR_KHR_android_surface_swapchain`
4. Re-validate session + swapchain + first-frame evidence with `CITRAVR_XR` tags.
5. Validate activity launch mode/intent behavior on physical headset for `VrActivity`.
6. Re-test input with headset-native interaction profile paths.
7. Evaluate whether `Failed to latch buffer` warnings persist on physical hardware.
8. Only after stable frame loop, re-enable optional features one by one (passthrough/foveation/perf controls) behind extension checks.

