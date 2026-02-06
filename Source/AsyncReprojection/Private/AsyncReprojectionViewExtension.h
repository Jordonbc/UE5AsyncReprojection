// Copyright © 2023–2026 Segritude Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SceneViewExtension.h"

class FAsyncReprojectionCameraTracker;

class FAsyncReprojectionViewExtension final : public FSceneViewExtensionBase
{
public:
	FAsyncReprojectionViewExtension(const FAutoRegister& AutoRegister, FAsyncReprojectionCameraTracker* InCameraTracker);

	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override;
	virtual void PreRenderView_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView) override;
	virtual void PrePostProcessPass_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& InView, const FPostProcessingInputs& Inputs) override;
	virtual void PostRenderViewFamily_RenderThread(FRDGBuilder& GraphBuilder, FSceneViewFamily& InViewFamily) override;

	virtual void SubscribeToPostProcessingPass(
		EPostProcessingPass Pass,
		const FSceneView& InView,
		FAfterPassCallbackDelegateArray& InOutPassCallbacks,
		bool bIsPassEnabled) override;

private:
	FScreenPassTexture PostProcessPass_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessMaterialInputs& Inputs, EPostProcessingPass PassId);

	bool ShouldRunForView(const FSceneView& View) const;

private:
	FAsyncReprojectionCameraTracker* CameraTracker = nullptr;

	struct FLatchedWarpState
	{
		bool bValid = false;
		FQuat DeltaQuat = FQuat::Identity;
		FVector DeltaTranslationCm = FVector::ZeroVector;
		float Weight = 0.0f;
		bool bDepthAvailable = false;
		bool bTranslationEnabled = false;
	};

	TMap<int32, FLatchedWarpState> LatchedWarpByPlayer;
	TMap<int32, EPostProcessingPass> LastAfterPassByPlayer;
	uint64 LastAfterPassFrameCounter = 0;

	struct FViewFamilyCapture
	{
		FRDGTextureRef ViewFamilyTexture = nullptr;
		TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer = nullptr;
		uint64 FrameCounter = 0;
	};

	mutable FRWLock ViewFamilyCaptureLock;
	FViewFamilyCapture ViewFamilyCaptureRT;
};
