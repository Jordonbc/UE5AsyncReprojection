// Copyright © 2023–2026 Segritude Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ScreenPass.h"

class FRDGBuilder;
class FSceneView;

struct FAsyncReprojectionOverlayData
{
	FString ModeString;
	FString WarpPointString;
	bool bActive = false;

	float FPS = 0.0f;
	float RefreshHz = 0.0f;

	FRotator DeltaRotDegrees = FRotator::ZeroRotator;
	float DeltaTransCm = 0.0f;

	bool bDepthAvailable = false;
	bool bTranslationEnabled = false;

	float Weight = 0.0f;
	float CpuSubmitMs = 0.0f;

	bool bAsyncPresentEnabled = false;
};

namespace AsyncReprojectionDebugOverlay
{
	void AddOverlayPass(FRDGBuilder& GraphBuilder, const FSceneView& View, const FScreenPassRenderTarget& Output, const FAsyncReprojectionOverlayData& Data);
}
