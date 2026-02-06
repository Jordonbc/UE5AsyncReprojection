# AsyncReprojection

AsyncReprojection is an optional UE 5.6+ rendering plugin that reduces *perceived* camera input latency by reprojecting (“late warping”) the last fully rendered scene image using a newer camera transform.

This is **not** frame generation and does **not** increase simulation or render FPS.

## Installation

1. Copy `Plugins/AsyncReprojection/` into your project’s `Plugins/` folder (or enable it if already present).
2. Enable the plugin in **Edit → Plugins → Rendering → AsyncReprojection**.
3. Restart the editor.
4. Configure **Project Settings → Plugins → Async Reprojection** (or via CVars).

## Runtime controls (CVars)

All settings are mirrored as hot-reloadable CVars under `r.AsyncReprojection.*`. Common ones:

- `r.AsyncReprojection.Mode` (`0=Off, 1=On, 2=Auto`)
- `r.AsyncReprojection.TimewarpMode` (`0=FullRender, 1=FreezeAndWarp, 2=DecimatedNoWarp, 3=DecimatedAndWarp`)
- `r.AsyncReprojection.EnableRotationWarp` (`0/1`)
- `r.AsyncReprojection.EnableTranslationWarp` (`0/1`)
- `r.AsyncReprojection.RequireDepthForTranslation` (`0/1`)
- `r.AsyncReprojection.WarpPoint` (`0=EndOfPostProcess, 1=PostRenderViewFamily (default)`)
- `r.AsyncReprojection.WarpAfterUI` (`0/1`) (warning: HUD warps; rotation-only)
- `r.AsyncReprojection.DebugOverlay` (`0/1`) (shows current state even when inactive)
- `r.AsyncReprojection.InputDrivenPose` (`0/1`) (adds rotation from mouse deltas after view build)
- `r.AsyncReprojection.InputYawDegreesPerPixel` (InputDrivenPose yaw scale)
- `r.AsyncReprojection.InputPitchDegreesPerPixel` (InputDrivenPose pitch scale)
- `r.AsyncReprojection.AsyncPresent` (`0/1`) (decimate world rendering and reproject cached frames at present rate)
- `r.AsyncReprojection.AsyncPresent.TargetWorldRenderFPS` (world render cadence when AsyncPresent is enabled)
- `r.AsyncReprojection.AsyncPresent.FreezeWorldRendering` (`0/1`) (forces world rendering off; useful for A/B tests)
- `r.AsyncReprojection.AsyncPresent.MaxCacheAgeMs` (fade out cached warp when too old)
- `r.AsyncReprojection.AsyncPresent.AllowHUDStable` (`0/1`) (attempt to warp before Slate UI so HUD stays stable)
- `r.AsyncReprojection.AsyncPresent.HUDMaskThreshold` (UI preservation threshold in HUD-stable composite path)
- `r.AsyncReprojection.AsyncPresent.ReprojectMovement` (`0/1`) (allow translation warp in cached present path)
- `r.AsyncReprojection.AsyncPresent.StretchBorders` (`0/1`) (black borders when off, clamped/stretch sampling when on)
- `r.AsyncReprojection.AsyncPresent.OcclusionFallback` (`0/1`) (local depth-neighbor fallback for disocclusion holes)

## How it works (high level)

AsyncReprojection supports two warp points:

- `PostRenderViewFamily` (default): warps the view family render target in `PostRenderViewFamily_RenderThread` (adds one copy to preserve a stable source).
- `EndOfPostProcess`: warps once per view using an after-pass callback during post processing.

In both cases the plugin:

1. Reads the latest camera transform (game-thread tracked snapshot).
2. Computes delta rotation and translation relative to the camera used to render the current frame.
3. Runs one full-screen RDG pass that warps `SceneColor` using `SceneDepth`:
	- Depth-aware reprojection for translation.
	- Rotation-only fallback when depth is missing/invalid.

### AsyncPresent / Timewarp Modes

When async timewarp is enabled, the plugin can decimate world rendering while the window still presents near refresh by reusing a cached scene color + depth on skipped frames.

`r.AsyncReprojection.TimewarpMode` maps to Unity-style states:

- `0 FullRender`: render world every frame (no cached skipped-frame path).
- `1 FreezeAndWarp`: freeze world rendering and continuously warp frozen cached frame.
- `2 DecimatedNoWarp`: render world at target cadence and present cached frame without warp between captures.
- `3 DecimatedAndWarp`: render world at target cadence and warp cached frame between captures.

### Mapping choice (holes vs stability)

The depth-aware path uses an **inverse-mapping** approach (for each output pixel, iteratively searches for the source pixel that reprojects into it). This tends to be stable and reduces holes compared to a forward “scatter” warp, but it can still produce disocclusion artifacts (especially with large deltas).

## Testing checklist

1. **Camera flick latency perception**
	- Run on a high-refresh display (e.g., 120 Hz) and cap / observe render FPS below refresh.
	- Toggle `r.AsyncReprojection.Mode 0/1/2` and evaluate “feel” when rapidly flicking the mouse.
2. **AsyncPresent cached warp**
	- `r.AsyncReprojection.AsyncPresent 1`
	- `r.AsyncReprojection.TimewarpMode 3`
	- `r.AsyncReprojection.AsyncPresent.TargetWorldRenderFPS 10`
	- `r.AsyncReprojection.DebugOverlay 1`
	- Expect the 3D world to update at the target cadence while the window remains responsive; on skipped frames, a small debug marker appears near the top-right (green when translation is active, red when rotation-only fallback is used).
3. **Close-wall strafe**
	- Strafe near a wall to check parallax and translation correctness.
	- Expect some disocclusion artifacts; verify clamps/fade prevent extreme smearing.
4. **HUD stability**
	- Default: HUD should remain stable (warp occurs before UI).
	- If enabling `r.AsyncReprojection.WarpAfterUI 1`, expect HUD warping (rotation-only).
	- If non-UI pixels are preserved as UI, raise `r.AsyncReprojection.AsyncPresent.HUDMaskThreshold` (for example `0.08 -> 0.12`).
	- If thin UI details disappear, lower `r.AsyncReprojection.AsyncPresent.HUDMaskThreshold` (for example `0.08 -> 0.04`).
5. **Black flicker guard**
	- Keep `r.AsyncReprojection.AsyncPresent 1` with skip active and verify no black flashes during camera motion.
	- If cache becomes temporarily unavailable (resize/device transitions), expect fallback restoration or one forced world-render recovery frame rather than black flicker.
6. **Depth missing fallback**
	- Force a case where depth isn’t available and verify translation disables automatically.
7. **GPU capture**
	- Verify a single full-screen pass (plus optional copy if using `PostRenderViewFamily` / `WarpAfterUI`).

## Maintainer commands (do not run via agents)

- Launch with verbose logging and quick toggles:
	- `UnrealEditor \"Veydran.uproject\" -game -log -ExecCmds=\"r.AsyncReprojection.Mode 2;r.AsyncReprojection.DebugOverlay 1\"`
- Validate the plugin compiles in an editor build:
	- `Engine/Build/BatchFiles/RunUAT.bat BuildCookRun -project=\"Veydran.uproject\" -noP4 -build -skipcook -skipstage -nopak -platform=Win64 -clientconfig=Development`

## Reticle / gameplay alignment

Because the scene is warped using a newer camera transform than the one used to render the frame, gameplay features that rely on camera transforms (raycasts, aim assists, hit-scan, reticles) should use the **latest** transform to avoid mismatch.

- Blueprint helpers:
	- `UAsyncReprojectionBlueprintLibrary::GetAsyncReprojectionLatestCameraTransform`
	- `UAsyncReprojectionBlueprintLibrary::GetAsyncReprojectionDelta`
	- `UAsyncReprojectionBlueprintLibrary::SubmitAsyncReprojectionLatestCameraTransform` (optional override)

## Limitations / Warnings

- This feature does **not** “turn 60 FPS into 120 FPS”.
- Depth-based reprojection can artifact with translucency, particles, and disocclusions.
- Large camera deltas are clamped and will fade out to avoid severe artifacts.
- For non-XR camera control that only updates on the game tick, the “latest camera” may be identical to the rendered camera (delta ~= 0), so the warp can become a no-op. For best results, submit a late camera transform (for example from input events or a late game-thread hook) via `SubmitAsyncReprojectionLatestCameraTransform`.
- The `EndOfPostProcess` warp point uses renderer-side post-process callback plumbing (`FPostProcessMaterialInputs`). If your engine build does not expose the required headers, use `PostRenderViewFamily` (default) and/or disable `EndOfPostProcess`.
- XR/VR is not supported by this plugin version.
