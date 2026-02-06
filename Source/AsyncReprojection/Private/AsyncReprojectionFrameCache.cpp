// Copyright © 2023–2026 Segritude Ltd. All Rights Reserved.

#include "AsyncReprojectionFrameCache.h"

#include "AsyncReprojection.h"
#include "AsyncReprojectionCVars.h"

#include "PostProcess/PostProcessMaterialInputs.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderTargetPool.h"
#include "SceneRenderTargetParameters.h"
#include "ScreenPass.h"
#include "ShaderParameterStruct.h"

namespace AsyncReprojectionFrameCachePrivate
{
	static constexpr uint64 VerboseLogFrameInterval = 120;
	static uint64 LastVerboseUpdateFrame = 0;
	static uint64 LastMissingSceneTexturesWarnFrame = 0;
	static uint64 LastInvalidSceneColorWarnFrame = 0;
	static uint64 LastMissingTargetsErrorFrame = 0;

	class FCacheDepthDeviceZCS : public FGlobalShader
	{
	public:
		DECLARE_GLOBAL_SHADER(FCacheDepthDeviceZCS);
		SHADER_USE_PARAMETER_STRUCT(FCacheDepthDeviceZCS, FGlobalShader);

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER_STRUCT_INCLUDE(FViewShaderParameters, View)
			SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTexturesStruct)
			SHADER_PARAMETER(FVector4f, BufferSizeAndInvSize)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutDeviceZ)
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
		}
	};

	IMPLEMENT_GLOBAL_SHADER(FCacheDepthDeviceZCS, "/Plugin/AsyncReprojection/Private/AsyncReprojectionCacheDepth.usf", "MainCS", SF_Compute);
}

FAsyncReprojectionFrameCache& FAsyncReprojectionFrameCache::Get()
{
	static FAsyncReprojectionFrameCache Instance;
	return Instance;
}

void FAsyncReprojectionFrameCache::Update_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessMaterialInputs& Inputs)
{
	const FAsyncReprojectionCVarState CVarState = FAsyncReprojectionCVars::Get();
	if (!CVarState.bAsyncPresent)
	{
		return;
	}

	if (!Inputs.SceneTextures.SceneTextures)
	{
		if ((GFrameCounterRenderThread - AsyncReprojectionFrameCachePrivate::LastMissingSceneTexturesWarnFrame) >= AsyncReprojectionFrameCachePrivate::VerboseLogFrameInterval)
		{
			UE_LOG(LogAsyncReprojection, Warning, TEXT("FrameCache update skipped: SceneTextures uniform buffer is unavailable."));
			AsyncReprojectionFrameCachePrivate::LastMissingSceneTexturesWarnFrame = GFrameCounterRenderThread;
		}
		return;
	}

	const int32 PlayerIndex = View.PlayerIndex;

	FScreenPassTexture SceneColor = Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
	if (!SceneColor.IsValid())
	{
		if ((GFrameCounterRenderThread - AsyncReprojectionFrameCachePrivate::LastInvalidSceneColorWarnFrame) >= AsyncReprojectionFrameCachePrivate::VerboseLogFrameInterval)
		{
			UE_LOG(LogAsyncReprojection, Warning, TEXT("FrameCache update skipped: SceneColor is invalid."));
			AsyncReprojectionFrameCachePrivate::LastInvalidSceneColorWarnFrame = GFrameCounterRenderThread;
		}
		return;
	}

	const FIntPoint Extent = SceneColor.Texture->Desc.Extent;
	if (Extent.X <= 0 || Extent.Y <= 0)
	{
		UE_LOG(LogAsyncReprojection, Fatal, TEXT("FrameCache received invalid SceneColor extent (%d x %d)."), Extent.X, Extent.Y);
		return;
	}

	const EPixelFormat ColorFormat = SceneColor.Texture->Desc.Format;

	FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
	EnsureTargets_RenderThread(RHICmdList, PlayerIndex, Extent, ColorFormat);

	TRefCountPtr<IPooledRenderTarget> ColorTarget;
	TRefCountPtr<IPooledRenderTarget> DepthTarget;
	{
		FRWScopeLock Lock(CacheLock, SLT_ReadOnly);
		if (const FCachedTargets* Existing = CacheByPlayer.Find(PlayerIndex))
		{
			ColorTarget = Existing->Color;
			DepthTarget = Existing->DepthDeviceZ;
		}
	}

	if (!ColorTarget.IsValid() || !DepthTarget.IsValid())
	{
		if ((GFrameCounterRenderThread - AsyncReprojectionFrameCachePrivate::LastMissingTargetsErrorFrame) >= AsyncReprojectionFrameCachePrivate::VerboseLogFrameInterval)
		{
			UE_LOG(LogAsyncReprojection, Error, TEXT("FrameCache update failed: missing cached targets for PlayerIndex=%d after allocation."), PlayerIndex);
			AsyncReprojectionFrameCachePrivate::LastMissingTargetsErrorFrame = GFrameCounterRenderThread;
		}
		return;
	}

	FRDGTextureRef ColorExternal = GraphBuilder.RegisterExternalTexture(ColorTarget, TEXT("AsyncReprojection.CachedColor"));
	FRDGTextureRef DepthExternal = GraphBuilder.RegisterExternalTexture(DepthTarget, TEXT("AsyncReprojection.CachedDepthDeviceZ"));

	AddCopyTexturePass(GraphBuilder, SceneColor.Texture, ColorExternal);

	{
		FRDGTextureUAVRef DepthUAV = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(DepthExternal, 0));

		AsyncReprojectionFrameCachePrivate::FCacheDepthDeviceZCS::FParameters* PassParameters =
			GraphBuilder.AllocParameters<AsyncReprojectionFrameCachePrivate::FCacheDepthDeviceZCS::FParameters>();

		PassParameters->View.View = View.ViewUniformBuffer;
		PassParameters->View.InstancedView = View.GetInstancedViewUniformBuffer();
		PassParameters->SceneTexturesStruct = Inputs.SceneTextures.SceneTextures;

		PassParameters->BufferSizeAndInvSize = FVector4f(float(Extent.X), float(Extent.Y), 1.0f / float(Extent.X), 1.0f / float(Extent.Y));
		PassParameters->OutDeviceZ = DepthUAV;

		TShaderMapRef<AsyncReprojectionFrameCachePrivate::FCacheDepthDeviceZCS> ComputeShader(GetGlobalShaderMap(View.GetFeatureLevel()));

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("AsyncReprojection CacheDepthDeviceZ"),
			ComputeShader,
			PassParameters,
			FIntVector(
				FMath::DivideAndRoundUp(Extent.X, 8),
				FMath::DivideAndRoundUp(Extent.Y, 8),
				1));
	}

		FAsyncReprojectionCachedFrameConstants Constants;
		Constants.bValid = true;
		Constants.ViewRect = View.UnscaledViewRect.IsEmpty() ? FIntRect(FIntPoint::ZeroValue, Extent) : View.UnscaledViewRect;
		Constants.BufferExtent = Extent;
		Constants.RenderedRotation = View.ViewRotation.Quaternion();
		Constants.RenderedLocation = View.ViewLocation;
	Constants.PreViewTranslation = View.ViewMatrices.GetPreViewTranslation();
	Constants.ViewToClip = FMatrix44f(View.ViewMatrices.GetProjectionMatrix());
	Constants.ClipToView = FMatrix44f(View.ViewMatrices.GetInvProjectionMatrix());
	Constants.RenderedSVPositionToTranslatedWorld = ComputeSVPositionToTranslatedWorld(View, Constants.ViewRect, Extent);
	Constants.FeatureLevel = View.GetFeatureLevel();
	Constants.RenderThreadFrameCounter = GFrameCounterRenderThread;
	Constants.CaptureTimeSeconds = FPlatformTime::Seconds();

		{
			FRWScopeLock Lock(CacheLock, SLT_Write);
			FCachedTargets& Slot = CacheByPlayer.FindOrAdd(PlayerIndex);
			Slot.Constants = Constants;
		}

	if (PlayerIndex >= 0 && PlayerIndex < MaxCachedPlayers)
	{
		LastValidCaptureFrameCounter[PlayerIndex].store(GFrameCounterRenderThread, std::memory_order_relaxed);
		LastCaptureTimeSeconds[PlayerIndex].store(Constants.CaptureTimeSeconds, std::memory_order_relaxed);
	}

	if ((GFrameCounterRenderThread - AsyncReprojectionFrameCachePrivate::LastVerboseUpdateFrame) >= AsyncReprojectionFrameCachePrivate::VerboseLogFrameInterval)
	{
		UE_LOG(
			LogAsyncReprojection,
			Verbose,
			TEXT("FrameCache updated: PlayerIndex=%d Extent=%dx%d Format=%d Frame=%llu"),
			PlayerIndex,
			Extent.X,
			Extent.Y,
			int32(ColorFormat),
			GFrameCounterRenderThread);
		AsyncReprojectionFrameCachePrivate::LastVerboseUpdateFrame = GFrameCounterRenderThread;
	}
}

bool FAsyncReprojectionFrameCache::GetCachedFrame_RenderThread(int32 PlayerIndex, TRefCountPtr<IPooledRenderTarget>& OutColor, TRefCountPtr<IPooledRenderTarget>& OutDepthDeviceZ, FAsyncReprojectionCachedFrameConstants& OutConstants) const
{
	FRWScopeLock Lock(CacheLock, SLT_ReadOnly);
	const FCachedTargets* Cached = CacheByPlayer.Find(PlayerIndex);
	if (Cached == nullptr || !Cached->Constants.bValid || !Cached->Color.IsValid() || !Cached->DepthDeviceZ.IsValid())
	{
		return false;
	}

	OutColor = Cached->Color;
	OutDepthDeviceZ = Cached->DepthDeviceZ;
	OutConstants = Cached->Constants;
	return true;
}

bool FAsyncReprojectionFrameCache::HasCachedFrame_AnyThread(int32 PlayerIndex) const
{
	if (PlayerIndex < 0 || PlayerIndex >= MaxCachedPlayers)
	{
		return false;
	}

	return LastValidCaptureFrameCounter[PlayerIndex].load(std::memory_order_relaxed) != 0;
}

bool FAsyncReprojectionFrameCache::HasUsableCachedFrame_AnyThread(int32 PlayerIndex, double NowSeconds, int32 MaxCacheAgeMs) const
{
	if (PlayerIndex < 0 || PlayerIndex >= MaxCachedPlayers)
	{
		return false;
	}

	const uint64 CaptureFrameCounter = LastValidCaptureFrameCounter[PlayerIndex].load(std::memory_order_relaxed);
	if (CaptureFrameCounter == 0)
	{
		return false;
	}

	const double CaptureTimeSeconds = LastCaptureTimeSeconds[PlayerIndex].load(std::memory_order_relaxed);
	if (CaptureTimeSeconds <= 0.0)
	{
		return false;
	}

	const double CacheAgeMs = (NowSeconds - CaptureTimeSeconds) * 1000.0;
	const double MaxAllowedAgeMs = FMath::Max(33.0, double(MaxCacheAgeMs));
	if (CacheAgeMs > MaxAllowedAgeMs)
	{
		return false;
	}

	return true;
}

double FAsyncReprojectionFrameCache::GetLastCaptureTimeSeconds_AnyThread(int32 PlayerIndex) const
{
	if (PlayerIndex < 0 || PlayerIndex >= MaxCachedPlayers)
	{
		return 0.0;
	}

	return LastCaptureTimeSeconds[PlayerIndex].load(std::memory_order_relaxed);
}

void FAsyncReprojectionFrameCache::EnsureTargets_RenderThread(FRHICommandListImmediate& RHICmdList, int32 PlayerIndex, const FIntPoint& Extent, EPixelFormat ColorFormat)
{
	bool bNeedsAlloc = false;
	{
		FRWScopeLock Lock(CacheLock, SLT_ReadOnly);
		if (const FCachedTargets* Existing = CacheByPlayer.Find(PlayerIndex))
		{
			if (!Existing->Color.IsValid() || !Existing->DepthDeviceZ.IsValid())
			{
				bNeedsAlloc = true;
			}
			else
			{
				const FIntPoint ExistingExtent = Existing->Color->GetDesc().Extent;
				bNeedsAlloc = (ExistingExtent != Extent) || (Existing->Color->GetDesc().Format != ColorFormat);
			}
		}
		else
		{
			bNeedsAlloc = true;
		}
	}

	if (!bNeedsAlloc)
	{
		return;
	}

	FPooledRenderTargetDesc ColorDesc = FPooledRenderTargetDesc::Create2DDesc(
		Extent,
		ColorFormat,
		FClearValueBinding::Transparent,
		TexCreate_None,
		TexCreate_ShaderResource | TexCreate_RenderTargetable,
		false);

	FPooledRenderTargetDesc DepthDesc = FPooledRenderTargetDesc::Create2DDesc(
		Extent,
		PF_R32_FLOAT,
		FClearValueBinding::Black,
		TexCreate_None,
		TexCreate_ShaderResource | TexCreate_UAV | TexCreate_RenderTargetable,
		false);

	TRefCountPtr<IPooledRenderTarget> NewColor;
	TRefCountPtr<IPooledRenderTarget> NewDepth;
	GRenderTargetPool.FindFreeElement(RHICmdList, ColorDesc, NewColor, TEXT("AsyncReprojection.CachedColor"));
	GRenderTargetPool.FindFreeElement(RHICmdList, DepthDesc, NewDepth, TEXT("AsyncReprojection.CachedDepthDeviceZ"));

	{
		FRWScopeLock Lock(CacheLock, SLT_Write);
		FCachedTargets& Slot = CacheByPlayer.FindOrAdd(PlayerIndex);
		Slot.Color = NewColor;
		Slot.DepthDeviceZ = NewDepth;
		Slot.Constants.bValid = false;
	}

	if (PlayerIndex >= 0 && PlayerIndex < MaxCachedPlayers)
	{
		LastValidCaptureFrameCounter[PlayerIndex].store(0, std::memory_order_relaxed);
		LastCaptureTimeSeconds[PlayerIndex].store(0.0, std::memory_order_relaxed);
	}
}

void FAsyncReprojectionFrameCache::EnsurePresentFallback_RenderThread(FRHICommandListImmediate& RHICmdList, int32 PlayerIndex, const FIntPoint& Extent, EPixelFormat ColorFormat)
{
	bool bNeedsAlloc = false;
	{
		FRWScopeLock Lock(CacheLock, SLT_ReadOnly);
		if (const FCachedTargets* Existing = CacheByPlayer.Find(PlayerIndex))
		{
			if (!Existing->PresentFallbackColor.IsValid())
			{
				bNeedsAlloc = true;
			}
			else
			{
				const FIntPoint ExistingExtent = Existing->PresentFallbackColor->GetDesc().Extent;
				bNeedsAlloc = (ExistingExtent != Extent) || (Existing->PresentFallbackColor->GetDesc().Format != ColorFormat);
			}
		}
		else
		{
			bNeedsAlloc = true;
		}
	}

	if (!bNeedsAlloc)
	{
		return;
	}

	FPooledRenderTargetDesc FallbackDesc = FPooledRenderTargetDesc::Create2DDesc(
		Extent,
		ColorFormat,
		FClearValueBinding::Black,
		TexCreate_None,
		TexCreate_ShaderResource | TexCreate_RenderTargetable,
		false);

	TRefCountPtr<IPooledRenderTarget> NewFallback;
	GRenderTargetPool.FindFreeElement(RHICmdList, FallbackDesc, NewFallback, TEXT("AsyncReprojection.PresentFallbackColor"));

	{
		FRWScopeLock Lock(CacheLock, SLT_Write);
		FCachedTargets& Slot = CacheByPlayer.FindOrAdd(PlayerIndex);
		Slot.PresentFallbackColor = NewFallback;
		Slot.bPresentFallbackValid = false;
	}
}

bool FAsyncReprojectionFrameCache::GetPresentFallback_RenderThread(int32 PlayerIndex, TRefCountPtr<IPooledRenderTarget>& OutFallbackColor) const
{
	FRWScopeLock Lock(CacheLock, SLT_ReadOnly);
	const FCachedTargets* Cached = CacheByPlayer.Find(PlayerIndex);
	if (Cached == nullptr || !Cached->bPresentFallbackValid || !Cached->PresentFallbackColor.IsValid())
	{
		return false;
	}

	OutFallbackColor = Cached->PresentFallbackColor;
	return true;
}

bool FAsyncReprojectionFrameCache::GetPresentFallbackTarget_RenderThread(int32 PlayerIndex, TRefCountPtr<IPooledRenderTarget>& OutFallbackColor) const
{
	FRWScopeLock Lock(CacheLock, SLT_ReadOnly);
	const FCachedTargets* Cached = CacheByPlayer.Find(PlayerIndex);
	if (Cached == nullptr || !Cached->PresentFallbackColor.IsValid())
	{
		return false;
	}

	OutFallbackColor = Cached->PresentFallbackColor;
	return true;
}

void FAsyncReprojectionFrameCache::SetPresentFallbackValid_RenderThread(int32 PlayerIndex, bool bValid)
{
	FRWScopeLock Lock(CacheLock, SLT_Write);
	if (FCachedTargets* Cached = CacheByPlayer.Find(PlayerIndex))
	{
		Cached->bPresentFallbackValid = bValid && Cached->PresentFallbackColor.IsValid();
	}
}

FMatrix44f FAsyncReprojectionFrameCache::ComputeSVPositionToTranslatedWorld(const FSceneView& View, const FIntRect& ViewRect, const FIntPoint& RasterContextSize)
{
	const FVector4f ViewSizeAndInvSize(
		float(ViewRect.Width()),
		float(ViewRect.Height()),
		1.0f / float(ViewRect.Width()),
		1.0f / float(ViewRect.Height()));

	const FVector2D RcpRasterContextSize(1.0 / double(RasterContextSize.X), 1.0 / double(RasterContextSize.Y));

	const float Mx = 2.0f * ViewSizeAndInvSize.Z;
	const float My = -2.0f * ViewSizeAndInvSize.W;
	const float Ax = -1.0f - 2.0f * float(ViewRect.Min.X) * ViewSizeAndInvSize.Z;
	const float Ay = 1.0f + 2.0f * float(ViewRect.Min.Y) * ViewSizeAndInvSize.W;

	const FMatrix ClipToViewRect(
		FPlane(Mx, 0, 0, 0),
		FPlane(0, My, 0, 0),
		FPlane(0, 0, 1, 0),
		FPlane(Ax, Ay, 0, 1));

	const FMatrix InvTranslatedVP = View.ViewMatrices.GetInvTranslatedViewProjectionMatrix();
	return FMatrix44f(ClipToViewRect * InvTranslatedVP);
}
