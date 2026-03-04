# Android XR Headset Checklist (Do Not Execute Until Instructed)

## Goal
Move from emulator bring-up to reliable headset validation with explicit OpenXR evidence and controlled risk.

## Pre-flight
1. Confirm branch is `androidxr-experiment`.
2. Install latest canary debug APK from this branch.
3. Confirm headset is visible in `adb devices -l`.
4. Clear logcat before each test pass.

## Runtime capability checks
1. Launch `VrActivity` and collect `CITRAVR_XR` / `CITRAVR_PORT` logs.
2. Confirm runtime identity:
   - `Runtime=Android XR`
   - runtime version logged.
3. Confirm required extensions are present and enabled:
   - `XR_KHR_opengl_es_enable`
   - `XR_KHR_android_surface_swapchain`
4. Archive extension dump and `xrGetSystemProperties` output.

## Session and frame-loop checks
1. Verify:
   - `xrCreateSession => XR_SUCCESS`
   - swapchain creation succeeds (`xrCreateSwapchainAndroidSurfaceKHR`).
2. Verify first-frame logs:
   - `Frame[1] xrWaitFrame`
   - `Frame[1] xrBeginFrame`
   - `Frame[1] xrEndFrame`
3. Verify sustained rendering:
   - repeated `doFrame presenting`
   - repeated `eglSwapBuffers ok frame=...`

## Failure handling checks
1. Launch with a known-invalid boot path.
2. Confirm VR fallback behavior:
   - VR error message/toast appears.
   - app returns to `MainActivity` (no stuck black full-space state).
3. Confirm logs include:
   - loader failure reason
   - `VR load failure ... returning to MainActivity`.

## Compatibility and feature gates
1. Keep Quest-specific features gated by extension checks.
2. Keep controller checks disabled until stable session/render is confirmed.
3. Do not enable passthrough/foveation/perf tuning for this phase unless required to unblock startup.

## Exit criteria for headset phase 1
1. App launches reliably from home space to full-space VR.
2. Session + swapchain + sustained frame loop are evidenced in logs.
3. Invalid launch paths fail gracefully back to menu.
4. OpenXR extension/system capability snapshot is archived.

## Deferred (next phase only)
1. Input parity and controller mapping polish.
2. Passthrough/MR features.
3. Performance tuning and frame pacing optimization.
