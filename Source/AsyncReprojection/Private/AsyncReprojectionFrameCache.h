// Copyright © 2023–2026 Segritude Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include <atomic>

class FRDGBuilder;
class FSceneView;

struct FPostProcessMaterialInputs;
template<typename T> class TRefCountPtr;
struct IPooledRenderTarget;

/**
 * @struct FAsyncReprojectionCachedFrameConstants
 *
 * GPU-independent constants captured from the last fully rendered frame for cached-frame reprojection.
 */
struct FAsyncReprojectionCachedFrameConstants
{
	bool bValid = false;

	FIntRect ViewRect = FIntRect(0, 0, 0, 0);
	FIntPoint BufferExtent = FIntPoint(0, 0);

	FQuat RenderedRotation = FQuat::Identity;
	FVector RenderedLocation = FVector::ZeroVector;
	FVector PreViewTranslation = FVector::ZeroVector;

	FMatrix44f ViewToClip = FMatrix44f::Identity;
	FMatrix44f ClipToView = FMatrix44f::Identity;

	FMatrix44f RenderedSVPositionToTranslatedWorld = FMatrix44f::Identity;

	ERHIFeatureLevel::Type FeatureLevel = ERHIFeatureLevel::SM5;

	uint64 RenderThreadFrameCounter = 0;
	double CaptureTimeSeconds = 0.0;
};

/**
 * @class FAsyncReprojectionFrameCache
 *
 * Stores the last fully rendered SceneColor and extracted device-Z depth for cached-frame reprojection.
 */
class FAsyncReprojectionFrameCache final
{
public:
	static FAsyncReprojectionFrameCache& Get();

	void Update_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessMaterialInputs& Inputs);

	bool GetCachedFrame_RenderThread(int32 PlayerIndex, TRefCountPtr<IPooledRenderTarget>& OutColor, TRefCountPtr<IPooledRenderTarget>& OutDepthDeviceZ, FAsyncReprojectionCachedFrameConstants& OutConstants) const;

	bool HasCachedFrame_AnyThread(int32 PlayerIndex) const;
	bool HasUsableCachedFrame_AnyThread(int32 PlayerIndex, double NowSeconds, int32 MaxCacheAgeMs) const;
	double GetLastCaptureTimeSeconds_AnyThread(int32 PlayerIndex) const;

	void EnsurePresentFallback_RenderThread(FRHICommandListImmediate& RHICmdList, int32 PlayerIndex, const FIntPoint& Extent, EPixelFormat ColorFormat);
	bool GetPresentFallback_RenderThread(int32 PlayerIndex, TRefCountPtr<IPooledRenderTarget>& OutFallbackColor) const;
	bool GetPresentFallbackTarget_RenderThread(int32 PlayerIndex, TRefCountPtr<IPooledRenderTarget>& OutFallbackColor) const;
	void SetPresentFallbackValid_RenderThread(int32 PlayerIndex, bool bValid);

private:
	FAsyncReprojectionFrameCache() = default;
	~FAsyncReprojectionFrameCache() = default;

	void EnsureTargets_RenderThread(FRHICommandListImmediate& RHICmdList, int32 PlayerIndex, const FIntPoint& Extent, EPixelFormat ColorFormat);

	static FMatrix44f ComputeSVPositionToTranslatedWorld(const FSceneView& View, const FIntRect& ViewRect, const FIntPoint& RasterContextSize);

private:
	struct FCachedTargets
	{
		TRefCountPtr<IPooledRenderTarget> Color;
		TRefCountPtr<IPooledRenderTarget> DepthDeviceZ;
		TRefCountPtr<IPooledRenderTarget> PresentFallbackColor;
		bool bPresentFallbackValid = false;
		FAsyncReprojectionCachedFrameConstants Constants;
	};

	mutable FRWLock CacheLock;
	TMap<int32, FCachedTargets> CacheByPlayer;

	static constexpr int32 MaxCachedPlayers = 4;
	std::atomic<uint64> LastValidCaptureFrameCounter[MaxCachedPlayers] = {};
	std::atomic<double> LastCaptureTimeSeconds[MaxCachedPlayers] = {};
};
