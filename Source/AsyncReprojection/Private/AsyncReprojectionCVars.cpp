// Copyright © 2023–2026 Segritude Ltd. All Rights Reserved.

#include "AsyncReprojectionCVars.h"

#include "AsyncReprojectionSettings.h"

#include "HAL/IConsoleManager.h"

namespace AsyncReprojectionCVars
{
	static TAutoConsoleVariable<int32> CVarMode(
		TEXT("r.AsyncReprojection.Mode"),
		1,
		TEXT("AsyncReprojection mode.\n")
		TEXT("0: Off\n")
		TEXT("1: On (default)\n")
		TEXT("2: Auto\n"),
		ECVF_RenderThreadSafe);

	static TAutoConsoleVariable<int32> CVarWarpPoint(
		TEXT("r.AsyncReprojection.WarpPoint"),
		1,
		TEXT("Where to apply the warp.\n")
		TEXT("0: EndOfPostProcess\n")
		TEXT("1: PostRenderViewFamily (default)\n"),
		ECVF_RenderThreadSafe);

	static TAutoConsoleVariable<int32> CVarTimewarpMode(
		TEXT("r.AsyncReprojection.TimewarpMode"),
		3,
		TEXT("Unity-style timewarp behavior mode.\n")
		TEXT("0: FullRender\n")
		TEXT("1: FreezeAndWarp\n")
		TEXT("2: DecimatedNoWarp\n")
		TEXT("3: DecimatedAndWarp (default)\n"),
		ECVF_RenderThreadSafe);

	static TAutoConsoleVariable<int32> CVarEnableRotationWarp(
		TEXT("r.AsyncReprojection.EnableRotationWarp"),
		1,
		TEXT("Enable rotation warping.\n"),
		ECVF_RenderThreadSafe);

	static TAutoConsoleVariable<int32> CVarEnableTranslationWarp(
		TEXT("r.AsyncReprojection.EnableTranslationWarp"),
		1,
		TEXT("Enable translation warping (depth-aware).\n"),
		ECVF_RenderThreadSafe);

	static TAutoConsoleVariable<int32> CVarRequireDepthForTranslation(
		TEXT("r.AsyncReprojection.RequireDepthForTranslation"),
		1,
		TEXT("If enabled, disables translation warp when depth is unavailable.\n"),
		ECVF_RenderThreadSafe);

	static TAutoConsoleVariable<int32> CVarWarpAfterUI(
		TEXT("r.AsyncReprojection.WarpAfterUI"),
		0,
		TEXT("If enabled, applies a rotation-only warp right before present (HUD will warp).\n"),
		ECVF_RenderThreadSafe);

	static TAutoConsoleVariable<int32> CVarAsyncPresent(
		TEXT("r.AsyncReprojection.AsyncPresent"),
		1,
		TEXT("Enable Async Present: decimate world rendering and reproject cached scene color on intermediate frames.\n"),
		ECVF_RenderThreadSafe);

	static TAutoConsoleVariable<float> CVarAsyncPresentTargetWorldRenderFPS(
		TEXT("r.AsyncReprojection.AsyncPresent.TargetWorldRenderFPS"),
		30.0f,
		TEXT("Async Present: target world render FPS.\n"),
		ECVF_RenderThreadSafe);

	static TAutoConsoleVariable<int32> CVarAsyncPresentFreezeWorldRendering(
		TEXT("r.AsyncReprojection.AsyncPresent.FreezeWorldRendering"),
		0,
		TEXT("Async Present: freeze world rendering after a cached frame is available.\n"),
		ECVF_RenderThreadSafe);

	static TAutoConsoleVariable<int32> CVarAsyncPresentMaxCacheAgeMs(
		TEXT("r.AsyncReprojection.AsyncPresent.MaxCacheAgeMs"),
		250,
		TEXT("Async Present: cache age (ms) after which cached-frame warp fades to 0.\n"),
		ECVF_RenderThreadSafe);

	static TAutoConsoleVariable<int32> CVarAsyncPresentAllowHUDStable(
		TEXT("r.AsyncReprojection.AsyncPresent.AllowHUDStable"),
		1,
		TEXT("Async Present: preserve Slate UI while compositing the cached warp (HUD remains stable; heuristic mask).\n"),
		ECVF_RenderThreadSafe);

	static TAutoConsoleVariable<float> CVarAsyncPresentHudMaskThreshold(
		TEXT("r.AsyncReprojection.AsyncPresent.HUDMaskThreshold"),
		0.08f,
		TEXT("Async Present: UI mask threshold used when preserving HUD with world-delta/alpha detection (higher = fewer pixels treated as UI).\n"),
		ECVF_RenderThreadSafe);

	static TAutoConsoleVariable<int32> CVarAsyncPresentReprojectMovement(
		TEXT("r.AsyncReprojection.AsyncPresent.ReprojectMovement"),
		1,
		TEXT("Async Present: allow translation warp on cached frames using cached depth.\n"),
		ECVF_RenderThreadSafe);

	static TAutoConsoleVariable<int32> CVarAsyncPresentStretchBorders(
		TEXT("r.AsyncReprojection.AsyncPresent.StretchBorders"),
		0,
		TEXT("Async Present: stretch/clamp out-of-bounds samples instead of returning black borders.\n"),
		ECVF_RenderThreadSafe);

	static TAutoConsoleVariable<int32> CVarAsyncPresentOcclusionFallback(
		TEXT("r.AsyncReprojection.AsyncPresent.OcclusionFallback"),
		1,
		TEXT("Async Present: use local depth-based neighbor fallback to reduce disocclusion holes.\n"),
		ECVF_RenderThreadSafe);

	static TAutoConsoleVariable<int32> CVarInputDrivenPose(
		TEXT("r.AsyncReprojection.InputDrivenPose"),
		1,
		TEXT("If enabled, adds additional rotation warp derived from accumulated mouse deltas after the view was built.\n")
		TEXT("This is useful when the gameplay camera only updates on the game tick.\n"),
		ECVF_RenderThreadSafe);

	static TAutoConsoleVariable<float> CVarInputYawDegreesPerPixel(
		TEXT("r.AsyncReprojection.InputYawDegreesPerPixel"),
		0.02f,
		TEXT("InputDrivenPose: yaw degrees per mouse pixel (positive = move mouse right turns right).\n"),
		ECVF_RenderThreadSafe);

	static TAutoConsoleVariable<float> CVarInputPitchDegreesPerPixel(
		TEXT("r.AsyncReprojection.InputPitchDegreesPerPixel"),
		0.02f,
		TEXT("InputDrivenPose: pitch degrees per mouse pixel (positive = move mouse up pitches up).\n"),
		ECVF_RenderThreadSafe);

	static TAutoConsoleVariable<float> CVarMaxYawDeg(
		TEXT("r.AsyncReprojection.MaxYawDegreesPerFrame"),
		2.0f,
		TEXT("Maximum absolute yaw delta (deg) per frame.\n"),
		ECVF_RenderThreadSafe);

	static TAutoConsoleVariable<float> CVarMaxPitchDeg(
		TEXT("r.AsyncReprojection.MaxPitchDegreesPerFrame"),
		2.0f,
		TEXT("Maximum absolute pitch delta (deg) per frame.\n"),
		ECVF_RenderThreadSafe);

	static TAutoConsoleVariable<float> CVarMaxRollDeg(
		TEXT("r.AsyncReprojection.MaxRollDegreesPerFrame"),
		3.0f,
		TEXT("Maximum absolute roll delta (deg) per frame.\n"),
		ECVF_RenderThreadSafe);

	static TAutoConsoleVariable<float> CVarMaxTranslationCm(
		TEXT("r.AsyncReprojection.MaxTranslationCmPerFrame"),
		5.0f,
		TEXT("Maximum camera translation magnitude (cm) per frame.\n"),
		ECVF_RenderThreadSafe);

	static TAutoConsoleVariable<float> CVarAutoMinRefreshDeltaHz(
		TEXT("r.AsyncReprojection.AutoMinRefreshDeltaHz"),
		10.0f,
		TEXT("Auto mode: minimum (RefreshHz - FPS) to enable.\n"),
		ECVF_RenderThreadSafe);

	static TAutoConsoleVariable<int32> CVarAutoMinStableFPSWindowMs(
		TEXT("r.AsyncReprojection.AutoMinStableFPSWindowMs"),
		500,
		TEXT("Auto mode: window size (ms) for FPS stability checks.\n"),
		ECVF_RenderThreadSafe);

	static TAutoConsoleVariable<float> CVarAutoMaxFPSStdDev(
		TEXT("r.AsyncReprojection.AutoMaxFPSStdDev"),
		1.5f,
		TEXT("Auto mode: maximum FPS standard deviation within the stability window.\n"),
		ECVF_RenderThreadSafe);

	static TAutoConsoleVariable<float> CVarAutoMaxWarpDegrees(
		TEXT("r.AsyncReprojection.AutoMaxWarpDegrees"),
		1.5f,
		TEXT("Auto mode: maximum delta rotation magnitude (deg).\n"),
		ECVF_RenderThreadSafe);

	static TAutoConsoleVariable<float> CVarAutoMaxTranslationCm(
		TEXT("r.AsyncReprojection.AutoMaxTranslationCm"),
		3.0f,
		TEXT("Auto mode: maximum translation magnitude (cm).\n"),
		ECVF_RenderThreadSafe);

	static TAutoConsoleVariable<int32> CVarDebugOverlay(
		TEXT("r.AsyncReprojection.DebugOverlay"),
		0,
		TEXT("Enable debug overlay.\n"),
		ECVF_RenderThreadSafe);

	static TAutoConsoleVariable<int32> CVarDebugFreezeWarp(
		TEXT("r.AsyncReprojection.DebugFreezeWarp"),
		0,
		TEXT("Freeze warp parameters for A/B testing.\n"),
		ECVF_RenderThreadSafe);

	static TAutoConsoleVariable<float> CVarRefreshHzOverride(
		TEXT("r.AsyncReprojection.RefreshHzOverride"),
		0.0f,
		TEXT("Override display refresh rate (Hz). 0 = best effort.\n"),
		ECVF_RenderThreadSafe);

	static TAutoConsoleVariable<int32> CVarEnableInEditor(
		TEXT("r.AsyncReprojection.EnableInEditor"),
		0,
		TEXT("Allow the warp to run in editor viewports.\n"),
		ECVF_RenderThreadSafe);
}

void FAsyncReprojectionCVars::Init()
{
	const UAsyncReprojectionSettings* Settings = GetDefault<UAsyncReprojectionSettings>();
	if (Settings == nullptr)
	{
		return;
	}

	auto SetInt = [](const TCHAR* Name, int32 Value)
	{
		if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			Var->Set(Value, ECVF_SetByProjectSetting);
		}
	};

	auto SetFloat = [](const TCHAR* Name, float Value)
	{
		if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			Var->Set(Value, ECVF_SetByProjectSetting);
		}
	};

	SetInt(TEXT("r.AsyncReprojection.Mode"), static_cast<int32>(Settings->Mode));
	SetInt(TEXT("r.AsyncReprojection.WarpPoint"), static_cast<int32>(Settings->WarpPoint));
	SetInt(TEXT("r.AsyncReprojection.TimewarpMode"), static_cast<int32>(Settings->TimewarpMode));
	SetInt(TEXT("r.AsyncReprojection.EnableRotationWarp"), Settings->bEnableRotationWarp ? 1 : 0);
	SetInt(TEXT("r.AsyncReprojection.EnableTranslationWarp"), Settings->bEnableTranslationWarp ? 1 : 0);
	SetInt(TEXT("r.AsyncReprojection.RequireDepthForTranslation"), Settings->bRequireDepthForTranslation ? 1 : 0);
	SetInt(TEXT("r.AsyncReprojection.WarpAfterUI"), Settings->bWarpAfterUI ? 1 : 0);
	SetInt(TEXT("r.AsyncReprojection.AsyncPresent"), Settings->bAsyncPresent ? 1 : 0);
	SetFloat(TEXT("r.AsyncReprojection.AsyncPresent.TargetWorldRenderFPS"), Settings->AsyncPresentTargetWorldRenderFPS);
	SetInt(TEXT("r.AsyncReprojection.AsyncPresent.FreezeWorldRendering"), Settings->bAsyncPresentFreezeWorldRendering ? 1 : 0);
	SetInt(TEXT("r.AsyncReprojection.AsyncPresent.MaxCacheAgeMs"), Settings->AsyncPresentMaxCacheAgeMs);
	SetInt(TEXT("r.AsyncReprojection.AsyncPresent.AllowHUDStable"), Settings->bAsyncPresentAllowHUDStable ? 1 : 0);
	SetInt(TEXT("r.AsyncReprojection.AsyncPresent.ReprojectMovement"), Settings->bAsyncPresentReprojectMovement ? 1 : 0);
	SetInt(TEXT("r.AsyncReprojection.AsyncPresent.StretchBorders"), Settings->bAsyncPresentStretchBorders ? 1 : 0);
	SetInt(TEXT("r.AsyncReprojection.AsyncPresent.OcclusionFallback"), Settings->bAsyncPresentOcclusionFallback ? 1 : 0);
	SetInt(TEXT("r.AsyncReprojection.InputDrivenPose"), 1);
	SetFloat(TEXT("r.AsyncReprojection.InputYawDegreesPerPixel"), 0.02f);
	SetFloat(TEXT("r.AsyncReprojection.InputPitchDegreesPerPixel"), 0.02f);
	SetFloat(TEXT("r.AsyncReprojection.MaxYawDegreesPerFrame"), Settings->MaxYawDegreesPerFrame);
	SetFloat(TEXT("r.AsyncReprojection.MaxPitchDegreesPerFrame"), Settings->MaxPitchDegreesPerFrame);
	SetFloat(TEXT("r.AsyncReprojection.MaxRollDegreesPerFrame"), Settings->MaxRollDegreesPerFrame);
	SetFloat(TEXT("r.AsyncReprojection.MaxTranslationCmPerFrame"), Settings->MaxTranslationCmPerFrame);
	SetFloat(TEXT("r.AsyncReprojection.AutoMinRefreshDeltaHz"), Settings->AutoMinRefreshDeltaHz);
	SetInt(TEXT("r.AsyncReprojection.AutoMinStableFPSWindowMs"), Settings->AutoMinStableFPSWindowMs);
	SetFloat(TEXT("r.AsyncReprojection.AutoMaxFPSStdDev"), Settings->AutoMaxFPSStdDev);
	SetFloat(TEXT("r.AsyncReprojection.AutoMaxWarpDegrees"), Settings->AutoMaxWarpDegrees);
	SetFloat(TEXT("r.AsyncReprojection.AutoMaxTranslationCm"), Settings->AutoMaxTranslationCm);
	SetInt(TEXT("r.AsyncReprojection.DebugOverlay"), Settings->bDebugOverlay ? 1 : 0);
	SetInt(TEXT("r.AsyncReprojection.DebugFreezeWarp"), Settings->bDebugFreezeWarp ? 1 : 0);
	SetFloat(TEXT("r.AsyncReprojection.RefreshHzOverride"), 0.0f);
	SetInt(TEXT("r.AsyncReprojection.EnableInEditor"), 0);
}

static EAsyncReprojectionMode ToMode(int32 Value)
{
	switch (Value)
	{
	case 0: return EAsyncReprojectionMode::Off;
	case 1: return EAsyncReprojectionMode::On;
	default: return EAsyncReprojectionMode::Auto;
	}
}

static EAsyncReprojectionWarpPoint ToWarpPoint(int32 Value)
{
	return Value == 1 ? EAsyncReprojectionWarpPoint::PostRenderViewFamily : EAsyncReprojectionWarpPoint::EndOfPostProcess;
}

static EAsyncReprojectionTimewarpMode ToTimewarpMode(int32 Value)
{
	switch (Value)
	{
	case 0: return EAsyncReprojectionTimewarpMode::FullRender;
	case 1: return EAsyncReprojectionTimewarpMode::FreezeAndWarp;
	case 2: return EAsyncReprojectionTimewarpMode::DecimatedNoWarp;
	default: return EAsyncReprojectionTimewarpMode::DecimatedAndWarp;
	}
}

FAsyncReprojectionCVarState FAsyncReprojectionCVars::Get()
{
	FAsyncReprojectionCVarState Out;

	Out.Mode = ToMode(AsyncReprojectionCVars::CVarMode.GetValueOnAnyThread());
	Out.WarpPoint = ToWarpPoint(AsyncReprojectionCVars::CVarWarpPoint.GetValueOnAnyThread());
	Out.TimewarpMode = ToTimewarpMode(AsyncReprojectionCVars::CVarTimewarpMode.GetValueOnAnyThread());
	Out.bEnableRotationWarp = AsyncReprojectionCVars::CVarEnableRotationWarp.GetValueOnAnyThread() != 0;
	Out.bEnableTranslationWarp = AsyncReprojectionCVars::CVarEnableTranslationWarp.GetValueOnAnyThread() != 0;
	Out.bRequireDepthForTranslation = AsyncReprojectionCVars::CVarRequireDepthForTranslation.GetValueOnAnyThread() != 0;
	Out.bWarpAfterUI = AsyncReprojectionCVars::CVarWarpAfterUI.GetValueOnAnyThread() != 0;

	Out.bAsyncPresent = AsyncReprojectionCVars::CVarAsyncPresent.GetValueOnAnyThread() != 0;
	Out.AsyncPresentTargetWorldRenderFPS = AsyncReprojectionCVars::CVarAsyncPresentTargetWorldRenderFPS.GetValueOnAnyThread();
	Out.bAsyncPresentFreezeWorldRendering = AsyncReprojectionCVars::CVarAsyncPresentFreezeWorldRendering.GetValueOnAnyThread() != 0;
	Out.AsyncPresentMaxCacheAgeMs = AsyncReprojectionCVars::CVarAsyncPresentMaxCacheAgeMs.GetValueOnAnyThread();
	Out.bAsyncPresentAllowHUDStable = AsyncReprojectionCVars::CVarAsyncPresentAllowHUDStable.GetValueOnAnyThread() != 0;
	Out.bAsyncPresentReprojectMovement = AsyncReprojectionCVars::CVarAsyncPresentReprojectMovement.GetValueOnAnyThread() != 0;
	Out.bAsyncPresentStretchBorders = AsyncReprojectionCVars::CVarAsyncPresentStretchBorders.GetValueOnAnyThread() != 0;
	Out.bAsyncPresentOcclusionFallback = AsyncReprojectionCVars::CVarAsyncPresentOcclusionFallback.GetValueOnAnyThread() != 0;

	Out.bInputDrivenPose = AsyncReprojectionCVars::CVarInputDrivenPose.GetValueOnAnyThread() != 0;
	Out.InputYawDegreesPerPixel = AsyncReprojectionCVars::CVarInputYawDegreesPerPixel.GetValueOnAnyThread();
	Out.InputPitchDegreesPerPixel = AsyncReprojectionCVars::CVarInputPitchDegreesPerPixel.GetValueOnAnyThread();

	Out.MaxYawDegreesPerFrame = AsyncReprojectionCVars::CVarMaxYawDeg.GetValueOnAnyThread();
	Out.MaxPitchDegreesPerFrame = AsyncReprojectionCVars::CVarMaxPitchDeg.GetValueOnAnyThread();
	Out.MaxRollDegreesPerFrame = AsyncReprojectionCVars::CVarMaxRollDeg.GetValueOnAnyThread();
	Out.MaxTranslationCmPerFrame = AsyncReprojectionCVars::CVarMaxTranslationCm.GetValueOnAnyThread();

	Out.AutoMinRefreshDeltaHz = AsyncReprojectionCVars::CVarAutoMinRefreshDeltaHz.GetValueOnAnyThread();
	Out.AutoMinStableFPSWindowMs = AsyncReprojectionCVars::CVarAutoMinStableFPSWindowMs.GetValueOnAnyThread();
	Out.AutoMaxFPSStdDev = AsyncReprojectionCVars::CVarAutoMaxFPSStdDev.GetValueOnAnyThread();
	Out.AutoMaxWarpDegrees = AsyncReprojectionCVars::CVarAutoMaxWarpDegrees.GetValueOnAnyThread();
	Out.AutoMaxTranslationCm = AsyncReprojectionCVars::CVarAutoMaxTranslationCm.GetValueOnAnyThread();

	Out.bDebugOverlay = AsyncReprojectionCVars::CVarDebugOverlay.GetValueOnAnyThread() != 0;
	Out.bDebugFreezeWarp = AsyncReprojectionCVars::CVarDebugFreezeWarp.GetValueOnAnyThread() != 0;

	Out.RefreshHzOverride = AsyncReprojectionCVars::CVarRefreshHzOverride.GetValueOnAnyThread();
	Out.bEnableInEditor = AsyncReprojectionCVars::CVarEnableInEditor.GetValueOnAnyThread() != 0;

	return Out;
}
