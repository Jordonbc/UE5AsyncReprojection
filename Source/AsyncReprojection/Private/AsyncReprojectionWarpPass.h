// Copyright © 2023–2026 Segritude Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SceneRenderTargetParameters.h"
#include "ScreenPass.h"

class FRDGBuilder;
class SWindow;
class FRDGTexture;
class FSceneView;
class FRHIViewport;
class FRHICommandListImmediate;

struct FAsyncReprojectionWarpPassInputs
{
	FScreenPassTexture SceneColor;
	FScreenPassRenderTarget Output;
	TRDGUniformBufferBinding<FSceneTextureUniformParameters> SceneTexturesUniformBuffer;

	FMatrix44f TranslatedWorldToLatestClip = FMatrix44f::Identity;
	FMatrix44f DeltaRotationInv4x4 = FMatrix44f::Identity;

	float WarpWeight = 0.0f;
	bool bEnableTranslation = false;
};

namespace AsyncReprojectionWarp
{
	FScreenPassTexture AddWarpPass(FRDGBuilder& GraphBuilder, const FSceneView& View, const FAsyncReprojectionWarpPassInputs& Inputs);
}

namespace FAsyncReprojectionBackBufferWarp
{
	void AddPassIfEnabled(FRDGBuilder& GraphBuilder, SWindow& SlateWindow, FRDGTexture* BackBuffer);
}

namespace FAsyncReprojectionCachedPresentWarp
{
	void AddBackBufferPassIfEnabled(FRDGBuilder& GraphBuilder, SWindow& SlateWindow, FRDGTexture* BackBuffer);
	void AddPreSlatePassIfEnabled(FRHICommandListImmediate& RHICmdList, FRHIViewport* ViewportRHI);
}
