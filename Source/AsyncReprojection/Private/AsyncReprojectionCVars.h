// Copyright © 2023–2026 Segritude Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AsyncReprojectionTypes.h"

struct FAsyncReprojectionCVarState
{
	EAsyncReprojectionMode Mode = EAsyncReprojectionMode::Auto;
	EAsyncReprojectionWarpPoint WarpPoint = EAsyncReprojectionWarpPoint::PostRenderViewFamily;
	EAsyncReprojectionTimewarpMode TimewarpMode = EAsyncReprojectionTimewarpMode::DecimatedAndWarp;

	bool bAsyncPresent = false;
	float AsyncPresentTargetWorldRenderFPS = 30.0f;
	bool bAsyncPresentFreezeWorldRendering = false;
	int32 AsyncPresentMaxCacheAgeMs = 250;
	bool bAsyncPresentAllowHUDStable = true;
	bool bAsyncPresentReprojectMovement = true;
	bool bAsyncPresentStretchBorders = false;
	bool bAsyncPresentOcclusionFallback = true;

	bool bEnableRotationWarp = true;
	bool bEnableTranslationWarp = true;
	bool bRequireDepthForTranslation = true;
	bool bWarpAfterUI = false;

	bool bInputDrivenPose = false;
	float InputYawDegreesPerPixel = 0.0f;
	float InputPitchDegreesPerPixel = 0.0f;

	float MaxYawDegreesPerFrame = 2.0f;
	float MaxPitchDegreesPerFrame = 2.0f;
	float MaxRollDegreesPerFrame = 3.0f;
	float MaxTranslationCmPerFrame = 5.0f;

	float AutoMinRefreshDeltaHz = 10.0f;
	int32 AutoMinStableFPSWindowMs = 500;
	float AutoMaxFPSStdDev = 1.5f;
	float AutoMaxWarpDegrees = 1.5f;
	float AutoMaxTranslationCm = 3.0f;

	bool bDebugOverlay = false;
	bool bDebugFreezeWarp = false;

	float RefreshHzOverride = 0.0f;
	bool bEnableInEditor = false;
};

class FAsyncReprojectionCVars
{
public:
	static void Init();
	static FAsyncReprojectionCVarState Get();
};
