# Repository Guidelines

## Project Structure & Module Organization
- Plugin root: `Plugins/AsyncReprojection/`.
- Runtime module code: `Source/AsyncReprojection/`.
- Public API headers: `Source/AsyncReprojection/Public/`.
- Internal implementation: `Source/AsyncReprojection/Private/`.
- Plugin descriptor: `AsyncReprojection.uplugin`.
- Content and assets: `Content/`, `Resources/`, `Shaders/`.
- Generated output folders (`Binaries/`, `Intermediate/`) are build artifacts and should not be edited manually.

## Build, Test, and Development Commands
Agents do not run builds/tests in this repository. Share commands for maintainers:
- Launch project with plugin toggles:
	- `UnrealEditor "Veydran.uproject" -game -log -ExecCmds="r.AsyncReprojection.Mode 2;r.AsyncReprojection.DebugOverlay 1"`
- Validate compile in editor pipeline:
	- `Engine/Build/BatchFiles/RunUAT.bat BuildCookRun -project="Veydran.uproject" -noP4 -build -skipcook -skipstage -nopak -platform=Win64 -clientconfig=Development`

## Coding Style & Naming Conventions
- Follow Unreal Engine C++ conventions.
- Use tab indentation (visual width four) and braces on new lines.
- Keep module boundaries clean: expose only stable API in `Public/`; keep helpers/render internals in `Private/`.
- Naming patterns should match UE style (`UAsyncReprojectionBlueprintLibrary`, `FAsyncReprojectionFrameCache`, `bEnableTranslationWarp`).
- Prefer concise, intent-focused comments for non-obvious rendering logic.

## Testing Guidelines
- Verify behavior through Unreal automation or manual in-editor checks (maintainer-executed).
- Prioritize regressions around:
	- camera flick responsiveness (`r.AsyncReprojection.Mode 0/1/2`)
	- async present cadence (`r.AsyncReprojection.AsyncPresent 1`)
	- depth fallback and HUD stability (`r.AsyncReprojection.WarpAfterUI`)
- When submitting changes, include expected outcomes and repro steps.

## Commit & Pull Request Guidelines
- Keep commits focused and scoped to one concern (for example: input tracking, warp pass, debug overlay).
- Use clear, imperative commit titles (example: `Fix stale camera delta in async present path`).
- PRs should include:
	- concise summary and motivation
	- linked task/issue
	- validation evidence (logs, repro steps, or captures)
	- screenshots/video for visual behavior changes

## Agent-Specific Instructions
- Do not run scripts under `Scripts/`, do not execute builds/tests, and do not commit directly.
- Use transparent diffs (`apply_patch`) and avoid destructive git operations.
