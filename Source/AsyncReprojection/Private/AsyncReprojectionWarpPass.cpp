// Copyright © 2023–2026 Segritude Ltd. All Rights Reserved.

#include "AsyncReprojectionWarpPass.h"

#include "AsyncReprojection.h"
#include "AsyncReprojectionAsyncPresent.h"
#include "AsyncReprojectionCameraTracker.h"
#include "AsyncReprojectionCVars.h"
#include "AsyncReprojectionFrameCache.h"

#include "DynamicRHI.h"
#include "HAL/IConsoleManager.h"
#include "RHICommandList.h"
#include "RenderGraphUtils.h"
#include "ScreenPass.h"
#include "SceneRenderTargetParameters.h"
#include "ShaderParameterStruct.h"

namespace AsyncReprojectionWarpPrivate
{
	static constexpr uint64 VerboseLogFrameInterval = 120;
	static uint64 LastWarpPassVerboseFrame = 0;
	static uint64 LastCachedPreSlateWarnFrame = 0;
	static uint64 LastCachedBackBufferWarnFrame = 0;

	static FMatrix ToFMatrix(const FMatrix44f& In)
	{
		FMatrix Out(EForceInit::ForceInitToZero);
		for (int32 Row = 0; Row < 4; Row++)
		{
			for (int32 Col = 0; Col < 4; Col++)
			{
				Out.M[Row][Col] = In.M[Row][Col];
			}
		}
		return Out;
	}

	class FAsyncReprojectionWarpPS : public FGlobalShader
	{
	public:
		DECLARE_GLOBAL_SHADER(FAsyncReprojectionWarpPS);
		SHADER_USE_PARAMETER_STRUCT(FAsyncReprojectionWarpPS, FGlobalShader);

		class FUseTranslation : SHADER_PERMUTATION_BOOL("USE_TRANSLATION");
		using FPermutationDomain = TShaderPermutationDomain<FUseTranslation>;

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER_STRUCT_INCLUDE(FViewShaderParameters, View)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneColorTexture)
			SHADER_PARAMETER_SAMPLER(SamplerState, SceneColorSampler)

			SHADER_PARAMETER(FMatrix44f, TranslatedWorldToLatestClip)
			SHADER_PARAMETER(FMatrix44f, DeltaRotationInv4x4)
			SHADER_PARAMETER(float, WarpWeight)
			SHADER_PARAMETER(FVector2f, SceneColorInvSize)

			SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTexturesStruct)

			RENDER_TARGET_BINDING_SLOTS()
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
		}
	};

	IMPLEMENT_GLOBAL_SHADER(FAsyncReprojectionWarpPS, "/Plugin/AsyncReprojection/Private/AsyncReprojectionWarp.usf", "MainPS", SF_Pixel);

	class FAsyncReprojectionPresentWarpPS : public FGlobalShader
	{
	public:
		DECLARE_GLOBAL_SHADER(FAsyncReprojectionPresentWarpPS);
		SHADER_USE_PARAMETER_STRUCT(FAsyncReprojectionPresentWarpPS, FGlobalShader);

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D, BackBufferTexture)
			SHADER_PARAMETER_SAMPLER(SamplerState, BackBufferSampler)
			SHADER_PARAMETER(FVector4f, ViewRectMinAndSize)
			SHADER_PARAMETER(FMatrix44f, ViewToClip)
			SHADER_PARAMETER(FMatrix44f, ClipToView)
			SHADER_PARAMETER(FMatrix44f, DeltaRotationInv4x4)
			SHADER_PARAMETER(float, WarpWeight)
			SHADER_PARAMETER(FVector2f, BackBufferInvSize)
			RENDER_TARGET_BINDING_SLOTS()
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
		}
	};

	IMPLEMENT_GLOBAL_SHADER(FAsyncReprojectionPresentWarpPS, "/Plugin/AsyncReprojection/Private/AsyncReprojectionPresentWarp.usf", "MainPS", SF_Pixel);

	class FAsyncReprojectionCachedWarpPS : public FGlobalShader
	{
	public:
		DECLARE_GLOBAL_SHADER(FAsyncReprojectionCachedWarpPS);
		SHADER_USE_PARAMETER_STRUCT(FAsyncReprojectionCachedWarpPS, FGlobalShader);

		class FUseTranslation : SHADER_PERMUTATION_BOOL("USE_TRANSLATION");
		using FPermutationDomain = TShaderPermutationDomain<FUseTranslation>;

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D, CachedColorTexture)
			SHADER_PARAMETER_SAMPLER(SamplerState, CachedColorSampler)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D, CachedDepthDeviceZTexture)
			SHADER_PARAMETER_SAMPLER(SamplerState, CachedDepthSampler)

			SHADER_PARAMETER(FMatrix44f, RenderedSVPositionToTranslatedWorld)
			SHADER_PARAMETER(FMatrix44f, TranslatedWorldToLatestClip)
			SHADER_PARAMETER(FMatrix44f, DeltaRotationInv4x4)
			SHADER_PARAMETER(FMatrix44f, ViewToClip)
			SHADER_PARAMETER(FMatrix44f, ClipToView)

				SHADER_PARAMETER(FVector4f, ViewRectMinAndSize)
				SHADER_PARAMETER(FVector4f, BufferSizeAndInvSize)
				SHADER_PARAMETER(float, WarpWeight)
				SHADER_PARAMETER(FVector2f, CachedInvSize)
				SHADER_PARAMETER(uint32, DebugOverlay)

				RENDER_TARGET_BINDING_SLOTS()
			END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
		}
	};

	IMPLEMENT_GLOBAL_SHADER(FAsyncReprojectionCachedWarpPS, "/Plugin/AsyncReprojection/Private/AsyncReprojectionCachedWarp.usf", "MainPS", SF_Pixel);

	class FAsyncReprojectionCachedWarpCompositePS : public FGlobalShader
	{
	public:
		DECLARE_GLOBAL_SHADER(FAsyncReprojectionCachedWarpCompositePS);
		SHADER_USE_PARAMETER_STRUCT(FAsyncReprojectionCachedWarpCompositePS, FGlobalShader);

		class FUseTranslation : SHADER_PERMUTATION_BOOL("USE_TRANSLATION");
		using FPermutationDomain = TShaderPermutationDomain<FUseTranslation>;

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D, CachedColorTexture)
			SHADER_PARAMETER_SAMPLER(SamplerState, CachedColorSampler)
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D, CachedDepthDeviceZTexture)
			SHADER_PARAMETER_SAMPLER(SamplerState, CachedDepthSampler)

			SHADER_PARAMETER_RDG_TEXTURE(Texture2D, UiTexture)
			SHADER_PARAMETER_SAMPLER(SamplerState, UiSampler)

			SHADER_PARAMETER(FMatrix44f, RenderedSVPositionToTranslatedWorld)
			SHADER_PARAMETER(FMatrix44f, TranslatedWorldToLatestClip)
			SHADER_PARAMETER(FMatrix44f, DeltaRotationInv4x4)
			SHADER_PARAMETER(FMatrix44f, ViewToClip)
			SHADER_PARAMETER(FMatrix44f, ClipToView)

			SHADER_PARAMETER(FVector4f, ViewRectMinAndSize)
			SHADER_PARAMETER(FVector4f, BufferSizeAndInvSize)
			SHADER_PARAMETER(float, WarpWeight)
			SHADER_PARAMETER(FVector2f, CachedInvSize)
			SHADER_PARAMETER(FVector2f, UiInvSize)
			SHADER_PARAMETER(float, UiMaskThreshold)
			SHADER_PARAMETER(uint32, DebugOverlay)

			RENDER_TARGET_BINDING_SLOTS()
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
		}
	};

	IMPLEMENT_GLOBAL_SHADER(FAsyncReprojectionCachedWarpCompositePS, "/Plugin/AsyncReprojection/Private/AsyncReprojectionCachedWarpComposite.usf", "MainPS", SF_Pixel);

	static bool IsWarpAfterUIEnabled()
	{
		const FAsyncReprojectionCVarState CVarState = FAsyncReprojectionCVars::Get();
		return CVarState.bWarpAfterUI;
	}
}

FScreenPassTexture AsyncReprojectionWarp::AddWarpPass(FRDGBuilder& GraphBuilder, const FSceneView& View, const FAsyncReprojectionWarpPassInputs& Inputs)
{
	check(Inputs.SceneColor.IsValid());
	check(Inputs.Output.IsValid());

	const FScreenPassTextureViewport InputViewport(Inputs.SceneColor);
	const FScreenPassTextureViewport OutputViewport(Inputs.Output);

	const FScreenPassViewInfo ViewInfo(View);

	TShaderMapRef<FScreenPassVS> VertexShader(GetGlobalShaderMap(ViewInfo.FeatureLevel));

	const bool bDoTranslation = Inputs.bEnableTranslation && Inputs.SceneTexturesUniformBuffer;
	if ((GFrameCounterRenderThread - AsyncReprojectionWarpPrivate::LastWarpPassVerboseFrame) >= AsyncReprojectionWarpPrivate::VerboseLogFrameInterval)
	{
		UE_LOG(
			LogAsyncReprojection,
			Verbose,
			TEXT("AddWarpPass: PlayerIndex=%d Translation=%d Weight=%.3f"),
			View.PlayerIndex,
			bDoTranslation ? 1 : 0,
			Inputs.WarpWeight);
		AsyncReprojectionWarpPrivate::LastWarpPassVerboseFrame = GFrameCounterRenderThread;
	}

	AsyncReprojectionWarpPrivate::FAsyncReprojectionWarpPS::FPermutationDomain PermutationVector;
	PermutationVector.Set<AsyncReprojectionWarpPrivate::FAsyncReprojectionWarpPS::FUseTranslation>(bDoTranslation);

	TShaderMapRef<AsyncReprojectionWarpPrivate::FAsyncReprojectionWarpPS> PixelShader(GetGlobalShaderMap(ViewInfo.FeatureLevel), PermutationVector);

	AsyncReprojectionWarpPrivate::FAsyncReprojectionWarpPS::FParameters* PassParameters =
		GraphBuilder.AllocParameters<AsyncReprojectionWarpPrivate::FAsyncReprojectionWarpPS::FParameters>();

	PassParameters->View.View = View.ViewUniformBuffer;
	PassParameters->View.InstancedView = View.GetInstancedViewUniformBuffer();
	PassParameters->SceneColorTexture = Inputs.SceneColor.Texture;
	PassParameters->SceneColorSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	PassParameters->TranslatedWorldToLatestClip = Inputs.TranslatedWorldToLatestClip;
	PassParameters->DeltaRotationInv4x4 = Inputs.DeltaRotationInv4x4;
	PassParameters->WarpWeight = Inputs.WarpWeight;

	const FIntPoint SceneColorExtent = Inputs.SceneColor.Texture->Desc.Extent;
	PassParameters->SceneColorInvSize = FVector2f(1.0f / float(SceneColorExtent.X), 1.0f / float(SceneColorExtent.Y));

	if (bDoTranslation)
	{
		PassParameters->SceneTexturesStruct = Inputs.SceneTexturesUniformBuffer;
	}
	else
	{
		PassParameters->SceneTexturesStruct = TRDGUniformBufferBinding<FSceneTextureUniformParameters>();
	}

	PassParameters->RenderTargets[0] = Inputs.Output.GetRenderTargetBinding();

	AddDrawScreenPass(
		GraphBuilder,
		RDG_EVENT_NAME("AsyncReprojection Warp %s", bDoTranslation ? TEXT("Depth") : TEXT("RotationOnly")),
		ViewInfo,
		OutputViewport,
		InputViewport,
		VertexShader,
		PixelShader,
		PassParameters,
		EScreenPassDrawFlags::None);

	return FScreenPassTexture(Inputs.Output);
}

void FAsyncReprojectionBackBufferWarp::AddPassIfEnabled(FRDGBuilder& GraphBuilder, SWindow& SlateWindow, FRDGTexture* BackBuffer)
{
	(void)SlateWindow;

	const FAsyncReprojectionCVarState CVarState = FAsyncReprojectionCVars::Get();
	if (CVarState.bAsyncPresent && FAsyncReprojectionAsyncPresent::Get().ShouldSkipWorldRendering())
	{
		return;
	}

	if (!AsyncReprojectionWarpPrivate::IsWarpAfterUIEnabled() || BackBuffer == nullptr)
	{
		return;
	}

	if (!CVarState.bEnableRotationWarp)
	{
		return;
	}

	const FAsyncReprojectionCameraSnapshot LatestCamera = FAsyncReprojectionCameraTracker::Get().GetLatestCamera(0);
	if (!LatestCamera.bIsValid)
	{
		return;
	}

	const FAsyncReprojectionRenderedViewSnapshot RenderedView = FAsyncReprojectionCameraTracker::Get().GetLatestRenderedView_RenderThread(0);
	if (!RenderedView.bIsValid)
	{
		return;
	}

	const float FPS = FAsyncReprojectionCameraTracker::Get().GetTrackedFPS();
	const float FPSStdDev = FAsyncReprojectionCameraTracker::Get().GetTrackedFPSStdDev();
	const float RefreshHz = FAsyncReprojectionCameraTracker::Get().GetTrackedRefreshHz();

	const FQuat LatestRotation = LatestCamera.CameraTransform.GetRotation();
	const FQuat RenderedRotation = RenderedView.RenderedRotation;

	const FQuat RawDeltaQuat = LatestRotation * RenderedRotation.Inverse();
	const FRotator RawDeltaRot = RawDeltaQuat.Rotator();
	const float AbsYaw = FMath::Abs(RawDeltaRot.Yaw);
	const float AbsPitch = FMath::Abs(RawDeltaRot.Pitch);
	const float AbsRoll = FMath::Abs(RawDeltaRot.Roll);
	const float MaxRot = FMath::Max3(AbsYaw, AbsPitch, AbsRoll);

	const float MaxRotClamp = FMath::Max3(CVarState.MaxYawDegreesPerFrame, CVarState.MaxPitchDegreesPerFrame, CVarState.MaxRollDegreesPerFrame);

	float Weight = 1.0f;
	if (MaxRotClamp > 0.0f)
	{
		Weight *= FMath::Clamp(1.0f - FMath::Max(0.0f, (MaxRot - MaxRotClamp) / (0.5f * MaxRotClamp + 1e-3f)), 0.0f, 1.0f);
	}

	bool bActive = (CVarState.Mode == EAsyncReprojectionMode::On);
	if (CVarState.Mode == EAsyncReprojectionMode::Auto)
	{
		const bool bHasRefresh = RefreshHz > 1.0f;
		const bool bRefreshDeltaOk = bHasRefresh && (RefreshHz - FPS) >= CVarState.AutoMinRefreshDeltaHz;
		const bool bStable = FPSStdDev <= CVarState.AutoMaxFPSStdDev;
		const bool bWarpSmall = (MaxRot <= CVarState.AutoMaxWarpDegrees);
		bActive = bRefreshDeltaOk && bStable && bWarpSmall;
	}

	if (!bActive || Weight <= 0.0f)
	{
		return;
	}

	FRotator ClampedRot = RawDeltaRot;
	ClampedRot.Yaw = FMath::Clamp(ClampedRot.Yaw, -CVarState.MaxYawDegreesPerFrame, CVarState.MaxYawDegreesPerFrame);
	ClampedRot.Pitch = FMath::Clamp(ClampedRot.Pitch, -CVarState.MaxPitchDegreesPerFrame, CVarState.MaxPitchDegreesPerFrame);
	ClampedRot.Roll = FMath::Clamp(ClampedRot.Roll, -CVarState.MaxRollDegreesPerFrame, CVarState.MaxRollDegreesPerFrame);

	const FQuat UsedDeltaQuat = ClampedRot.Quaternion();

	FRDGTextureRef BackBufferRef = static_cast<FRDGTextureRef>(BackBuffer);
	const FRDGTextureDesc BackBufferDesc = BackBufferRef->Desc;

	FRDGTextureRef Temp = GraphBuilder.CreateTexture(BackBufferDesc, TEXT("AsyncReprojection.BackBufferTemp"));
	AddCopyTexturePass(GraphBuilder, BackBufferRef, Temp);

	const FIntRect FullRect(FIntPoint::ZeroValue, BackBufferDesc.Extent);
	const FIntRect ViewRect = RenderedView.ViewRect.IsEmpty() ? FullRect : RenderedView.ViewRect;

	FScreenPassTexture Input(Temp, ViewRect);
	FScreenPassRenderTarget Output(BackBufferRef, ViewRect, ERenderTargetLoadAction::ELoad);

	const FScreenPassTextureViewport InputViewport(Input);
	const FScreenPassTextureViewport OutputViewport(Output);
	const FScreenPassViewInfo ViewInfo(RenderedView.FeatureLevel);

	TShaderMapRef<FScreenPassVS> VertexShader(GetGlobalShaderMap(ViewInfo.FeatureLevel));
	TShaderMapRef<AsyncReprojectionWarpPrivate::FAsyncReprojectionPresentWarpPS> PixelShader(GetGlobalShaderMap(ViewInfo.FeatureLevel));

	AsyncReprojectionWarpPrivate::FAsyncReprojectionPresentWarpPS::FParameters* PassParameters =
		GraphBuilder.AllocParameters<AsyncReprojectionWarpPrivate::FAsyncReprojectionPresentWarpPS::FParameters>();
	PassParameters->BackBufferTexture = Input.Texture;
	PassParameters->BackBufferSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	PassParameters->ViewRectMinAndSize = FVector4f(float(ViewRect.Min.X), float(ViewRect.Min.Y), float(ViewRect.Width()), float(ViewRect.Height()));
	PassParameters->ViewToClip = RenderedView.ViewToClip;
	PassParameters->ClipToView = RenderedView.ClipToView;
	PassParameters->DeltaRotationInv4x4 = FMatrix44f(FQuatRotationMatrix(UsedDeltaQuat.Inverse()));
	PassParameters->WarpWeight = Weight;
	PassParameters->BackBufferInvSize = FVector2f(1.0f / float(BackBufferDesc.Extent.X), 1.0f / float(BackBufferDesc.Extent.Y));
	PassParameters->RenderTargets[0] = Output.GetRenderTargetBinding();

	AddDrawScreenPass(
		GraphBuilder,
		RDG_EVENT_NAME("AsyncReprojection WarpAfterUI (RotationOnly)"),
		ViewInfo,
		OutputViewport,
		InputViewport,
		VertexShader,
		PixelShader,
		PassParameters,
		EScreenPassDrawFlags::None);
}

static FQuat ComputeInputDrivenDeltaQuat_RenderThread(const FAsyncReprojectionCVarState& CVarState, const FAsyncReprojectionRenderedViewSnapshot& RenderedViewSnapshot, const FQuat& RenderedRotation)
{
	if (!CVarState.bInputDrivenPose || !RenderedViewSnapshot.bIsValid)
	{
		return FQuat::Identity;
	}

	const float YawScale = CVarState.InputYawDegreesPerPixel;
	const float PitchScale = CVarState.InputPitchDegreesPerPixel;
	if (YawScale == 0.0f && PitchScale == 0.0f)
	{
		return FQuat::Identity;
	}

	const FVector2f CurrentTotals = FAsyncReprojectionCameraTracker::Get().GetMouseTotals_RenderThread(0);
	const float DeltaX = CurrentTotals.X - RenderedViewSnapshot.InputMouseXTotal;
	const float DeltaY = CurrentTotals.Y - RenderedViewSnapshot.InputMouseYTotal;

	const float YawDeg = DeltaX * YawScale;
	const float PitchDeg = (-DeltaY) * PitchScale;

	const FQuat YawQuat(FVector::UpVector, FMath::DegreesToRadians(YawDeg));
	const FVector PitchAxis = RenderedRotation.RotateVector(FVector::RightVector);
	const FQuat PitchQuat(PitchAxis, FMath::DegreesToRadians(PitchDeg));
	return PitchQuat * YawQuat;
}

static float GetAsyncPresentUiMaskThreshold()
{
	static const auto CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.AsyncReprojection.AsyncPresent.HUDMaskThreshold"));
	if (CVar != nullptr)
	{
		return CVar->GetFloat();
	}
	return 0.08f;
}

static bool TryRestorePresentFallback(FRDGBuilder& GraphBuilder, FRDGTexture* BackBuffer, int32 PlayerIndex)
{
	if (BackBuffer == nullptr)
	{
		return false;
	}

	TRefCountPtr<IPooledRenderTarget> FallbackColor;
	if (!FAsyncReprojectionFrameCache::Get().GetPresentFallback_RenderThread(PlayerIndex, FallbackColor) || !FallbackColor.IsValid())
	{
		return false;
	}

	FRDGTextureRef BackBufferRDG = static_cast<FRDGTextureRef>(BackBuffer);
	FRDGTextureRef FallbackRDG = GraphBuilder.RegisterExternalTexture(FallbackColor, TEXT("AsyncReprojection.PresentFallbackColorRT"));
	if (FallbackRDG == nullptr || BackBufferRDG == nullptr)
	{
		return false;
	}

	const FRDGTextureDesc BackBufferDesc = BackBufferRDG->Desc;
	const FRDGTextureDesc FallbackDesc = FallbackRDG->Desc;
	if (FallbackDesc.Extent != BackBufferDesc.Extent || FallbackDesc.Format != BackBufferDesc.Format)
	{
		FAsyncReprojectionFrameCache::Get().SetPresentFallbackValid_RenderThread(PlayerIndex, false);
		return false;
	}

	AddCopyTexturePass(GraphBuilder, FallbackRDG, BackBufferRDG);
	return true;
}

void FAsyncReprojectionCachedPresentWarp::AddPreSlatePassIfEnabled(FRHICommandListImmediate& RHICmdList, FRHIViewport* ViewportRHI)
{
	if (ViewportRHI == nullptr)
	{
		return;
	}

	const FAsyncReprojectionCVarState CVarState = FAsyncReprojectionCVars::Get();
	if (!CVarState.bAsyncPresent || !CVarState.bAsyncPresentAllowHUDStable)
	{
		return;
	}

	if (!FAsyncReprojectionAsyncPresent::Get().ShouldSkipWorldRendering())
	{
		return;
	}

	TRefCountPtr<IPooledRenderTarget> CachedColor;
	TRefCountPtr<IPooledRenderTarget> CachedDepthDeviceZ;
	FAsyncReprojectionCachedFrameConstants CachedConstants;
	if (!FAsyncReprojectionFrameCache::Get().GetCachedFrame_RenderThread(0, CachedColor, CachedDepthDeviceZ, CachedConstants))
	{
		FAsyncReprojectionAsyncPresent::Get().ReportCacheMiss_RenderThread();
		if ((GFrameCounterRenderThread - AsyncReprojectionWarpPrivate::LastCachedPreSlateWarnFrame) >= AsyncReprojectionWarpPrivate::VerboseLogFrameInterval)
		{
			UE_LOG(LogAsyncReprojection, Warning, TEXT("AsyncPresent PreSlate warp skipped: cached frame is not available."));
			AsyncReprojectionWarpPrivate::LastCachedPreSlateWarnFrame = GFrameCounterRenderThread;
		}
		return;
	}

	if (!CachedConstants.bValid || !CachedColor.IsValid() || !CachedDepthDeviceZ.IsValid())
	{
		FAsyncReprojectionAsyncPresent::Get().ReportCacheMiss_RenderThread();
		if ((GFrameCounterRenderThread - AsyncReprojectionWarpPrivate::LastCachedPreSlateWarnFrame) >= AsyncReprojectionWarpPrivate::VerboseLogFrameInterval)
		{
			UE_LOG(LogAsyncReprojection, Error, TEXT("AsyncPresent PreSlate warp skipped: cached frame resources are invalid."));
			AsyncReprojectionWarpPrivate::LastCachedPreSlateWarnFrame = GFrameCounterRenderThread;
		}
		return;
	}

	const float RefreshHz = FAsyncReprojectionCameraTracker::Get().GetTrackedRefreshHz();
	const float FPS = FAsyncReprojectionCameraTracker::Get().GetTrackedFPS();
	const float FPSStdDev = FAsyncReprojectionCameraTracker::Get().GetTrackedFPSStdDev();

	const FAsyncReprojectionCameraSnapshot LatestCamera = FAsyncReprojectionCameraTracker::Get().GetLatestCamera(0);
	if (!LatestCamera.bIsValid)
	{
		FAsyncReprojectionAsyncPresent::Get().ReportCacheMiss_RenderThread();
		if ((GFrameCounterRenderThread - AsyncReprojectionWarpPrivate::LastCachedPreSlateWarnFrame) >= AsyncReprojectionWarpPrivate::VerboseLogFrameInterval)
		{
			UE_LOG(LogAsyncReprojection, Warning, TEXT("AsyncPresent PreSlate warp skipped: latest camera is invalid."));
			AsyncReprojectionWarpPrivate::LastCachedPreSlateWarnFrame = GFrameCounterRenderThread;
		}
		return;
	}

	FTextureRHIRef BackBufferTextureRHI = RHIGetViewportBackBuffer(ViewportRHI);
	if (!BackBufferTextureRHI.IsValid())
	{
		FAsyncReprojectionAsyncPresent::Get().ReportCacheMiss_RenderThread();
		if ((GFrameCounterRenderThread - AsyncReprojectionWarpPrivate::LastCachedPreSlateWarnFrame) >= AsyncReprojectionWarpPrivate::VerboseLogFrameInterval)
		{
			UE_LOG(LogAsyncReprojection, Error, TEXT("AsyncPresent PreSlate warp skipped: viewport back buffer is invalid."));
			AsyncReprojectionWarpPrivate::LastCachedPreSlateWarnFrame = GFrameCounterRenderThread;
		}
		return;
	}

	FRDGBuilder GraphBuilder(RHICmdList);

	FRDGTextureRef BackBufferRDG = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(BackBufferTextureRHI, TEXT("AsyncReprojection.PreSlateBackBuffer")));

	FRDGTextureRef CachedColorRDG = GraphBuilder.RegisterExternalTexture(CachedColor, TEXT("AsyncReprojection.CachedColorRT"));
	FRDGTextureRef CachedDepthRDG = GraphBuilder.RegisterExternalTexture(CachedDepthDeviceZ, TEXT("AsyncReprojection.CachedDepthDeviceZRT"));

	const FIntRect ViewRect = CachedConstants.ViewRect.IsEmpty()
		? FIntRect(FIntPoint::ZeroValue, CachedConstants.BufferExtent)
		: CachedConstants.ViewRect;

	const FQuat RenderedRotation = CachedConstants.RenderedRotation;
	const FVector RenderedLocation = CachedConstants.RenderedLocation;

	const FQuat LatestRotation = LatestCamera.CameraTransform.GetRotation();
	const FVector LatestLocation = LatestCamera.CameraTransform.GetLocation();

	FQuat RawDeltaQuat = LatestRotation * RenderedRotation.Inverse();
	FRotator RawDeltaRot = RawDeltaQuat.Rotator();
	FVector RawDeltaTranslation = LatestLocation - RenderedLocation;

	const FAsyncReprojectionRenderedViewSnapshot RenderedViewSnapshot = FAsyncReprojectionCameraTracker::Get().GetLatestRenderedView_RenderThread(0);
	const FQuat InputDeltaQuat = ComputeInputDrivenDeltaQuat_RenderThread(CVarState, RenderedViewSnapshot, RenderedRotation);
	RawDeltaQuat = InputDeltaQuat * RawDeltaQuat;
	RawDeltaRot = RawDeltaQuat.Rotator();

	if (!CVarState.bEnableRotationWarp)
	{
		RawDeltaQuat = FQuat::Identity;
		RawDeltaRot = FRotator::ZeroRotator;
	}
	if (!CVarState.bEnableTranslationWarp || !CVarState.bAsyncPresentReprojectMovement)
	{
		RawDeltaTranslation = FVector::ZeroVector;
	}

	const float TranslationMag = RawDeltaTranslation.Size();
	const float AbsYaw = FMath::Abs(RawDeltaRot.Yaw);
	const float AbsPitch = FMath::Abs(RawDeltaRot.Pitch);
	const float AbsRoll = FMath::Abs(RawDeltaRot.Roll);
	const float MaxRot = FMath::Max3(AbsYaw, AbsPitch, AbsRoll);

	bool bActive = (CVarState.Mode == EAsyncReprojectionMode::On);
	if (CVarState.Mode == EAsyncReprojectionMode::Auto)
	{
		const bool bHasRefresh = RefreshHz > 1.0f;
		const bool bRefreshDeltaOk = bHasRefresh && (RefreshHz - FPS) >= CVarState.AutoMinRefreshDeltaHz;
		const bool bStable = FPSStdDev <= CVarState.AutoMaxFPSStdDev;
		const bool bWarpSmall = (MaxRot <= CVarState.AutoMaxWarpDegrees) && (TranslationMag <= CVarState.AutoMaxTranslationCm);
		bActive = bRefreshDeltaOk && bStable && bWarpSmall;
	}
	if (!bActive)
	{
		return;
	}

	float CacheFade = 1.0f;
	if (CVarState.AsyncPresentMaxCacheAgeMs > 0)
	{
		const double AgeMs = (FPlatformTime::Seconds() - CachedConstants.CaptureTimeSeconds) * 1000.0;
		CacheFade = float(FMath::Clamp(1.0 - (AgeMs / double(CVarState.AsyncPresentMaxCacheAgeMs)), 0.0, 1.0));
	}

	float Weight = CacheFade;

	const float MaxRotClamp = FMath::Max3(CVarState.MaxYawDegreesPerFrame, CVarState.MaxPitchDegreesPerFrame, CVarState.MaxRollDegreesPerFrame);
	const float MaxTransClamp = CVarState.MaxTranslationCmPerFrame;
	if (MaxRotClamp > 0.0f)
	{
		Weight *= FMath::Clamp(1.0f - FMath::Max(0.0f, (MaxRot - MaxRotClamp) / (0.5f * MaxRotClamp + 1e-3f)), 0.0f, 1.0f);
	}
	if (MaxTransClamp > 0.0f)
	{
		Weight *= FMath::Clamp(1.0f - FMath::Max(0.0f, (TranslationMag - MaxTransClamp) / (0.5f * MaxTransClamp + 1e-3f)), 0.0f, 1.0f);
	}

	if (Weight <= 0.0f)
	{
		return;
	}

	UE_LOG(
		LogAsyncReprojection,
		VeryVerbose,
		TEXT("AsyncPresent PreSlate warp executing: Rotation=(P=%.3f Y=%.3f R=%.3f) TranslationCm=%.3f Weight=%.3f"),
		RawDeltaRot.Pitch,
		RawDeltaRot.Yaw,
		RawDeltaRot.Roll,
		TranslationMag,
		Weight);

	FRotator ClampedRot = RawDeltaRot;
	ClampedRot.Yaw = FMath::Clamp(ClampedRot.Yaw, -CVarState.MaxYawDegreesPerFrame, CVarState.MaxYawDegreesPerFrame);
	ClampedRot.Pitch = FMath::Clamp(ClampedRot.Pitch, -CVarState.MaxPitchDegreesPerFrame, CVarState.MaxPitchDegreesPerFrame);
	ClampedRot.Roll = FMath::Clamp(ClampedRot.Roll, -CVarState.MaxRollDegreesPerFrame, CVarState.MaxRollDegreesPerFrame);

	const FVector ClampedTranslation = (MaxTransClamp > 0.0f) ? RawDeltaTranslation.GetClampedToMaxSize(MaxTransClamp) : RawDeltaTranslation;

	const FQuat UsedDeltaQuat = ClampedRot.Quaternion();

	const FVector ClampedLatestLocation = RenderedLocation + ClampedTranslation;
	const FQuat ClampedLatestRotation = UsedDeltaQuat * RenderedRotation;

	const FVector LatestOriginPlusPreView = ClampedLatestLocation + CachedConstants.PreViewTranslation;
	const FMatrix LatestWorldToViewRotation = FInverseRotationMatrix(ClampedLatestRotation.Rotator());
	const FMatrix LatestTranslatedWorldToView = LatestWorldToViewRotation * FTranslationMatrix(-LatestOriginPlusPreView);

		const FMatrix44f Projection = CachedConstants.ViewToClip;
		const FMatrix LatestTranslatedWorldToClip = LatestTranslatedWorldToView * AsyncReprojectionWarpPrivate::ToFMatrix(Projection);

	const bool bDoTranslation = CVarState.bEnableTranslationWarp && CVarState.bAsyncPresentReprojectMovement;

	AsyncReprojectionWarpPrivate::FAsyncReprojectionCachedWarpPS::FPermutationDomain PermutationVector;
	PermutationVector.Set<AsyncReprojectionWarpPrivate::FAsyncReprojectionCachedWarpPS::FUseTranslation>(bDoTranslation);

	TShaderMapRef<FScreenPassVS> VertexShader(GetGlobalShaderMap(CachedConstants.FeatureLevel));
	TShaderMapRef<AsyncReprojectionWarpPrivate::FAsyncReprojectionCachedWarpPS> PixelShader(GetGlobalShaderMap(CachedConstants.FeatureLevel), PermutationVector);

	AsyncReprojectionWarpPrivate::FAsyncReprojectionCachedWarpPS::FParameters* PassParameters =
		GraphBuilder.AllocParameters<AsyncReprojectionWarpPrivate::FAsyncReprojectionCachedWarpPS::FParameters>();

	PassParameters->CachedColorTexture = CachedColorRDG;
	PassParameters->CachedColorSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	PassParameters->CachedDepthDeviceZTexture = CachedDepthRDG;
	PassParameters->CachedDepthSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

	PassParameters->RenderedSVPositionToTranslatedWorld = CachedConstants.RenderedSVPositionToTranslatedWorld;
	PassParameters->TranslatedWorldToLatestClip = FMatrix44f(LatestTranslatedWorldToClip);
	PassParameters->DeltaRotationInv4x4 = FMatrix44f(FQuatRotationMatrix(UsedDeltaQuat.Inverse()));
	PassParameters->ViewToClip = CachedConstants.ViewToClip;
	PassParameters->ClipToView = CachedConstants.ClipToView;

		PassParameters->ViewRectMinAndSize = FVector4f(float(ViewRect.Min.X), float(ViewRect.Min.Y), float(ViewRect.Width()), float(ViewRect.Height()));
		PassParameters->BufferSizeAndInvSize = FVector4f(float(CachedConstants.BufferExtent.X), float(CachedConstants.BufferExtent.Y), 1.0f / float(CachedConstants.BufferExtent.X), 1.0f / float(CachedConstants.BufferExtent.Y));
		PassParameters->WarpWeight = Weight;
		PassParameters->CachedInvSize = FVector2f(1.0f / float(CachedConstants.BufferExtent.X), 1.0f / float(CachedConstants.BufferExtent.Y));
		PassParameters->DebugOverlay = CVarState.bDebugOverlay ? 1u : 0u;
		PassParameters->RenderTargets[0] = FRenderTargetBinding(BackBufferRDG, ERenderTargetLoadAction::ELoad);

	const FScreenPassTextureViewport Viewport(ViewRect);
	const FScreenPassViewInfo ViewInfo(CachedConstants.FeatureLevel);

	AddDrawScreenPass(
		GraphBuilder,
		RDG_EVENT_NAME("AsyncReprojection AsyncPresent CachedWarp"),
		ViewInfo,
		Viewport,
		Viewport,
		VertexShader,
		PixelShader,
		PassParameters,
		EScreenPassDrawFlags::None);

	GraphBuilder.Execute();
}

void FAsyncReprojectionCachedPresentWarp::AddBackBufferPassIfEnabled(FRDGBuilder& GraphBuilder, SWindow& SlateWindow, FRDGTexture* BackBuffer)
{
	(void)SlateWindow;

	if (BackBuffer == nullptr)
	{
		return;
	}

	const FAsyncReprojectionCVarState CVarState = FAsyncReprojectionCVars::Get();
	if (!CVarState.bAsyncPresent)
	{
		return;
	}

	if (!FAsyncReprojectionAsyncPresent::Get().ShouldSkipWorldRendering())
	{
		return;
	}

	const int32 PlayerIndex = 0;
	FRDGTextureRef BackBufferRDG = static_cast<FRDGTextureRef>(BackBuffer);
	const FRDGTextureDesc BackBufferDesc = BackBufferRDG->Desc;

	FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
	FAsyncReprojectionFrameCache::Get().EnsurePresentFallback_RenderThread(RHICmdList, PlayerIndex, BackBufferDesc.Extent, BackBufferDesc.Format);

	TRefCountPtr<IPooledRenderTarget> CachedColor;
	TRefCountPtr<IPooledRenderTarget> CachedDepthDeviceZ;
	FAsyncReprojectionCachedFrameConstants CachedConstants;
	if (!FAsyncReprojectionFrameCache::Get().GetCachedFrame_RenderThread(PlayerIndex, CachedColor, CachedDepthDeviceZ, CachedConstants))
	{
		FAsyncReprojectionAsyncPresent::Get().ReportCacheMiss_RenderThread();
		const bool bRestoredFallback = TryRestorePresentFallback(GraphBuilder, BackBuffer, PlayerIndex);
		if (bRestoredFallback)
		{
			FAsyncReprojectionAsyncPresent::Get().ReportCompositeSuccess_RenderThread(FPlatformTime::Seconds());
		}
		if ((GFrameCounterRenderThread - AsyncReprojectionWarpPrivate::LastCachedBackBufferWarnFrame) >= AsyncReprojectionWarpPrivate::VerboseLogFrameInterval)
		{
			UE_LOG(LogAsyncReprojection, Warning, TEXT("AsyncPresent BackBuffer warp skipped: cached frame is not available (fallback restored=%d)."), bRestoredFallback ? 1 : 0);
			AsyncReprojectionWarpPrivate::LastCachedBackBufferWarnFrame = GFrameCounterRenderThread;
		}
		return;
	}

	if (!CachedConstants.bValid || !CachedColor.IsValid() || !CachedDepthDeviceZ.IsValid())
	{
		FAsyncReprojectionAsyncPresent::Get().ReportCacheMiss_RenderThread();
		const bool bRestoredFallback = TryRestorePresentFallback(GraphBuilder, BackBuffer, PlayerIndex);
		if (bRestoredFallback)
		{
			FAsyncReprojectionAsyncPresent::Get().ReportCompositeSuccess_RenderThread(FPlatformTime::Seconds());
		}
		if ((GFrameCounterRenderThread - AsyncReprojectionWarpPrivate::LastCachedBackBufferWarnFrame) >= AsyncReprojectionWarpPrivate::VerboseLogFrameInterval)
		{
			UE_LOG(LogAsyncReprojection, Error, TEXT("AsyncPresent BackBuffer warp skipped: cached frame resources are invalid (fallback restored=%d)."), bRestoredFallback ? 1 : 0);
			AsyncReprojectionWarpPrivate::LastCachedBackBufferWarnFrame = GFrameCounterRenderThread;
		}
		return;
	}

	FAsyncReprojectionCameraSnapshot LatestCamera = FAsyncReprojectionCameraTracker::Get().GetLatestCamera(PlayerIndex);
	if (!LatestCamera.bIsValid)
	{
		if ((GFrameCounterRenderThread - AsyncReprojectionWarpPrivate::LastCachedBackBufferWarnFrame) >= AsyncReprojectionWarpPrivate::VerboseLogFrameInterval)
		{
			UE_LOG(LogAsyncReprojection, Warning, TEXT("AsyncPresent BackBuffer warp: latest camera is invalid, using rendered camera fallback."));
			AsyncReprojectionWarpPrivate::LastCachedBackBufferWarnFrame = GFrameCounterRenderThread;
		}

		LatestCamera.bIsValid = true;
		LatestCamera.CameraTransform = FTransform(CachedConstants.RenderedRotation, CachedConstants.RenderedLocation);
	}

	const float RefreshHz = FAsyncReprojectionCameraTracker::Get().GetTrackedRefreshHz();
	const float FPS = FAsyncReprojectionCameraTracker::Get().GetTrackedFPS();
	const float FPSStdDev = FAsyncReprojectionCameraTracker::Get().GetTrackedFPSStdDev();

	const FIntRect ViewRect = CachedConstants.ViewRect.IsEmpty()
		? FIntRect(FIntPoint::ZeroValue, CachedConstants.BufferExtent)
		: CachedConstants.ViewRect;

	const FQuat RenderedRotation = CachedConstants.RenderedRotation;
	const FVector RenderedLocation = CachedConstants.RenderedLocation;

	const FQuat LatestRotation = LatestCamera.CameraTransform.GetRotation();
	const FVector LatestLocation = LatestCamera.CameraTransform.GetLocation();

	FQuat RawDeltaQuat = LatestRotation * RenderedRotation.Inverse();
	FRotator RawDeltaRot = RawDeltaQuat.Rotator();
	FVector RawDeltaTranslation = LatestLocation - RenderedLocation;

	const FAsyncReprojectionRenderedViewSnapshot RenderedViewSnapshot = FAsyncReprojectionCameraTracker::Get().GetLatestRenderedView_RenderThread(PlayerIndex);
	const FQuat InputDeltaQuat = ComputeInputDrivenDeltaQuat_RenderThread(CVarState, RenderedViewSnapshot, RenderedRotation);
	RawDeltaQuat = InputDeltaQuat * RawDeltaQuat;
	RawDeltaRot = RawDeltaQuat.Rotator();

	if (!CVarState.bEnableRotationWarp)
	{
		RawDeltaQuat = FQuat::Identity;
		RawDeltaRot = FRotator::ZeroRotator;
	}
	if (!CVarState.bEnableTranslationWarp || !CVarState.bAsyncPresentReprojectMovement)
	{
		RawDeltaTranslation = FVector::ZeroVector;
	}

	const float TranslationMag = RawDeltaTranslation.Size();
	const float AbsYaw = FMath::Abs(RawDeltaRot.Yaw);
	const float AbsPitch = FMath::Abs(RawDeltaRot.Pitch);
	const float AbsRoll = FMath::Abs(RawDeltaRot.Roll);
	const float MaxRot = FMath::Max3(AbsYaw, AbsPitch, AbsRoll);

	bool bActive = (CVarState.Mode == EAsyncReprojectionMode::On);
	if (CVarState.Mode == EAsyncReprojectionMode::Auto)
	{
		const bool bHasRefresh = RefreshHz > 1.0f;
		const bool bRefreshDeltaOk = bHasRefresh && (RefreshHz - FPS) >= CVarState.AutoMinRefreshDeltaHz;
		const bool bStable = FPSStdDev <= CVarState.AutoMaxFPSStdDev;
		const bool bWarpSmall = (MaxRot <= CVarState.AutoMaxWarpDegrees) && (TranslationMag <= CVarState.AutoMaxTranslationCm);
		bActive = bRefreshDeltaOk && bStable && bWarpSmall;
	}

	float CacheFade = 1.0f;
	if (CVarState.AsyncPresentMaxCacheAgeMs > 0)
	{
		const double AgeMs = (FPlatformTime::Seconds() - CachedConstants.CaptureTimeSeconds) * 1000.0;
		CacheFade = float(FMath::Clamp(1.0 - (AgeMs / double(CVarState.AsyncPresentMaxCacheAgeMs)), 0.0, 1.0));
	}

	float Weight = CacheFade;
	if (!bActive)
	{
		Weight = 0.0f;
	}

	const float MaxRotClamp = FMath::Max3(CVarState.MaxYawDegreesPerFrame, CVarState.MaxPitchDegreesPerFrame, CVarState.MaxRollDegreesPerFrame);
	const float MaxTransClamp = CVarState.MaxTranslationCmPerFrame;
	if (MaxRotClamp > 0.0f)
	{
		Weight *= FMath::Clamp(1.0f - FMath::Max(0.0f, (MaxRot - MaxRotClamp) / (0.5f * MaxRotClamp + 1e-3f)), 0.0f, 1.0f);
	}
	if (MaxTransClamp > 0.0f)
	{
		Weight *= FMath::Clamp(1.0f - FMath::Max(0.0f, (TranslationMag - MaxTransClamp) / (0.5f * MaxTransClamp + 1e-3f)), 0.0f, 1.0f);
	}

	Weight = FMath::Max(0.0f, Weight);

	UE_LOG(
		LogAsyncReprojection,
		VeryVerbose,
		TEXT("AsyncPresent BackBuffer warp executing: Rotation=(P=%.3f Y=%.3f R=%.3f) TranslationCm=%.3f Weight=%.3f"),
		RawDeltaRot.Pitch,
		RawDeltaRot.Yaw,
		RawDeltaRot.Roll,
		TranslationMag,
		Weight);

	FRotator ClampedRot = RawDeltaRot;
	ClampedRot.Yaw = FMath::Clamp(ClampedRot.Yaw, -CVarState.MaxYawDegreesPerFrame, CVarState.MaxYawDegreesPerFrame);
	ClampedRot.Pitch = FMath::Clamp(ClampedRot.Pitch, -CVarState.MaxPitchDegreesPerFrame, CVarState.MaxPitchDegreesPerFrame);
	ClampedRot.Roll = FMath::Clamp(ClampedRot.Roll, -CVarState.MaxRollDegreesPerFrame, CVarState.MaxRollDegreesPerFrame);

	const FVector ClampedTranslation = (MaxTransClamp > 0.0f) ? RawDeltaTranslation.GetClampedToMaxSize(MaxTransClamp) : RawDeltaTranslation;

	const FQuat UsedDeltaQuat = ClampedRot.Quaternion();

	const FVector ClampedLatestLocation = RenderedLocation + ClampedTranslation;
	const FQuat ClampedLatestRotation = UsedDeltaQuat * RenderedRotation;

	const FVector LatestOriginPlusPreView = ClampedLatestLocation + CachedConstants.PreViewTranslation;
	const FMatrix LatestWorldToViewRotation = FInverseRotationMatrix(ClampedLatestRotation.Rotator());
	const FMatrix LatestTranslatedWorldToView = LatestWorldToViewRotation * FTranslationMatrix(-LatestOriginPlusPreView);

	const FMatrix44f Projection = CachedConstants.ViewToClip;
	const FMatrix LatestTranslatedWorldToClip = LatestTranslatedWorldToView * AsyncReprojectionWarpPrivate::ToFMatrix(Projection);

	const bool bDoTranslation = CVarState.bEnableTranslationWarp && CVarState.bAsyncPresentReprojectMovement;

	FRDGTextureRef UiCopy = GraphBuilder.CreateTexture(BackBufferDesc, TEXT("AsyncReprojection.AsyncPresent.UiCopy"));
	AddCopyTexturePass(GraphBuilder, BackBufferRDG, UiCopy);

	FRDGTextureRef CachedColorRDG = GraphBuilder.RegisterExternalTexture(CachedColor, TEXT("AsyncReprojection.CachedColorRT"));
	FRDGTextureRef CachedDepthRDG = GraphBuilder.RegisterExternalTexture(CachedDepthDeviceZ, TEXT("AsyncReprojection.CachedDepthDeviceZRT"));

	AsyncReprojectionWarpPrivate::FAsyncReprojectionCachedWarpCompositePS::FPermutationDomain PermutationVector;
	PermutationVector.Set<AsyncReprojectionWarpPrivate::FAsyncReprojectionCachedWarpCompositePS::FUseTranslation>(bDoTranslation);

	TShaderMapRef<FScreenPassVS> VertexShader(GetGlobalShaderMap(CachedConstants.FeatureLevel));
	TShaderMapRef<AsyncReprojectionWarpPrivate::FAsyncReprojectionCachedWarpCompositePS> PixelShader(GetGlobalShaderMap(CachedConstants.FeatureLevel), PermutationVector);

	AsyncReprojectionWarpPrivate::FAsyncReprojectionCachedWarpCompositePS::FParameters* PassParameters =
		GraphBuilder.AllocParameters<AsyncReprojectionWarpPrivate::FAsyncReprojectionCachedWarpCompositePS::FParameters>();

	PassParameters->CachedColorTexture = CachedColorRDG;
	PassParameters->CachedColorSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	PassParameters->CachedDepthDeviceZTexture = CachedDepthRDG;
	PassParameters->CachedDepthSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

	PassParameters->UiTexture = UiCopy;
	PassParameters->UiSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

	PassParameters->RenderedSVPositionToTranslatedWorld = CachedConstants.RenderedSVPositionToTranslatedWorld;
	PassParameters->TranslatedWorldToLatestClip = FMatrix44f(LatestTranslatedWorldToClip);
	PassParameters->DeltaRotationInv4x4 = FMatrix44f(FQuatRotationMatrix(UsedDeltaQuat.Inverse()));
	PassParameters->ViewToClip = CachedConstants.ViewToClip;
	PassParameters->ClipToView = CachedConstants.ClipToView;

	PassParameters->ViewRectMinAndSize = FVector4f(float(ViewRect.Min.X), float(ViewRect.Min.Y), float(ViewRect.Width()), float(ViewRect.Height()));
	PassParameters->BufferSizeAndInvSize = FVector4f(float(CachedConstants.BufferExtent.X), float(CachedConstants.BufferExtent.Y), 1.0f / float(CachedConstants.BufferExtent.X), 1.0f / float(CachedConstants.BufferExtent.Y));
	PassParameters->WarpWeight = Weight;
	PassParameters->CachedInvSize = FVector2f(1.0f / float(CachedConstants.BufferExtent.X), 1.0f / float(CachedConstants.BufferExtent.Y));
	PassParameters->UiInvSize = FVector2f(1.0f / float(BackBufferDesc.Extent.X), 1.0f / float(BackBufferDesc.Extent.Y));
	PassParameters->UiMaskThreshold = GetAsyncPresentUiMaskThreshold();
	PassParameters->DebugOverlay = CVarState.bDebugOverlay ? 1u : 0u;
	PassParameters->RenderTargets[0] = FRenderTargetBinding(BackBufferRDG, ERenderTargetLoadAction::ELoad);

	const FScreenPassTextureViewport Viewport(ViewRect);
	const FScreenPassViewInfo ViewInfo(CachedConstants.FeatureLevel);

	AddDrawScreenPass(
		GraphBuilder,
		RDG_EVENT_NAME("AsyncReprojection AsyncPresent CachedWarpComposite"),
		ViewInfo,
		Viewport,
		Viewport,
		VertexShader,
		PixelShader,
		PassParameters,
		EScreenPassDrawFlags::None);
}
