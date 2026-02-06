// Copyright © 2023–2026 Segritude Ltd. All Rights Reserved.

#include "AsyncReprojectionViewExtension.h"

#include "AsyncReprojection.h"
#include "AsyncReprojectionCameraTracker.h"
#include "AsyncReprojectionCVars.h"
#include "AsyncReprojectionDebugOverlay.h"
#include "AsyncReprojectionFrameCache.h"
#include "AsyncReprojectionWarpPass.h"

#include "PostProcess/PostProcessInputs.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "RenderGraphUtils.h"
#include "SceneRenderTargetParameters.h"
#include "ScreenPass.h"
#include "UnrealClient.h"
#include "RendererInterface.h"

namespace AsyncReprojectionViewExtensionPrivate
{
	static constexpr uint64 VerboseLogFrameInterval = 120;
	static uint64 LastShouldRunSkipLogFrame = 0;
	static uint64 LastMissingCameraWarnFrame = 0;
}

static FQuat ComputeInputDrivenDeltaQuat(
	const FAsyncReprojectionCVarState& CVarState,
	int32 PlayerIndex,
	const FQuat& RenderedRotation,
	const FAsyncReprojectionRenderedViewSnapshot& RenderedViewSnapshot)
{
	if (!CVarState.bInputDrivenPose)
	{
		return FQuat::Identity;
	}

	if (!RenderedViewSnapshot.bIsValid)
	{
		return FQuat::Identity;
	}

	const float YawScale = CVarState.InputYawDegreesPerPixel;
	const float PitchScale = CVarState.InputPitchDegreesPerPixel;
	if (YawScale == 0.0f && PitchScale == 0.0f)
	{
		return FQuat::Identity;
	}

	const FVector2f CurrentTotals = FAsyncReprojectionCameraTracker::Get().GetMouseTotals_RenderThread(PlayerIndex);
	const float DeltaX = CurrentTotals.X - RenderedViewSnapshot.InputMouseXTotal;
	const float DeltaY = CurrentTotals.Y - RenderedViewSnapshot.InputMouseYTotal;

	const float YawDeg = DeltaX * YawScale;
	const float PitchDeg = (-DeltaY) * PitchScale;

	const FQuat YawQuat(FVector::UpVector, FMath::DegreesToRadians(YawDeg));
	const FVector PitchAxis = RenderedRotation.RotateVector(FVector::RightVector);
	const FQuat PitchQuat(PitchAxis, FMath::DegreesToRadians(PitchDeg));

	return PitchQuat * YawQuat;
}

FAsyncReprojectionViewExtension::FAsyncReprojectionViewExtension(const FAutoRegister& AutoRegister, FAsyncReprojectionCameraTracker* InCameraTracker)
	: FSceneViewExtensionBase(AutoRegister)
	, CameraTracker(InCameraTracker)
{
	FSceneViewExtensionIsActiveFunctor IsActiveFunctor;
	IsActiveFunctor.IsActiveFunction = [](const ISceneViewExtension* SceneViewExtension, const FSceneViewExtensionContext& Context)
	{
		return true;
	};
	IsActiveThisFrameFunctions.Add(IsActiveFunctor);
}

void FAsyncReprojectionViewExtension::SetupViewFamily(FSceneViewFamily& InViewFamily)
{
}

void FAsyncReprojectionViewExtension::PreRenderView_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView)
{
	if (!ShouldRunForView(InView))
	{
		return;
	}

	FAsyncReprojectionRenderedViewSnapshot Snapshot;
	Snapshot.bIsValid = true;
	Snapshot.RenderedRotation = InView.ViewRotation.Quaternion();
	Snapshot.RenderedLocation = InView.ViewLocation;
	const FVector2f MouseTotals = FAsyncReprojectionCameraTracker::Get().GetMouseTotals_RenderThread(InView.PlayerIndex);
	Snapshot.InputMouseXTotal = MouseTotals.X;
	Snapshot.InputMouseYTotal = MouseTotals.Y;
	Snapshot.ViewToClip = FMatrix44f(InView.ViewMatrices.GetProjectionMatrix());
	Snapshot.ClipToView = FMatrix44f(InView.ViewMatrices.GetInvProjectionMatrix());
	Snapshot.ViewRect = InView.UnscaledViewRect;
	Snapshot.FeatureLevel = InView.GetFeatureLevel();

	FAsyncReprojectionCameraTracker::Get().PublishRenderedView_RenderThread(InView.PlayerIndex, Snapshot);
}

void FAsyncReprojectionViewExtension::PrePostProcessPass_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& InView, const FPostProcessingInputs& Inputs)
{
	(void)GraphBuilder;

	const FAsyncReprojectionCVarState CVarState = FAsyncReprojectionCVars::Get();
	if (CVarState.WarpPoint != EAsyncReprojectionWarpPoint::PostRenderViewFamily)
	{
		return;
	}

	if (!ShouldRunForView(InView))
	{
		return;
	}

	FRWScopeLock Lock(ViewFamilyCaptureLock, SLT_Write);
	ViewFamilyCaptureRT.ViewFamilyTexture = Inputs.ViewFamilyTexture;
	ViewFamilyCaptureRT.SceneTexturesUniformBuffer = Inputs.SceneTextures;
	ViewFamilyCaptureRT.FrameCounter = GFrameCounterRenderThread;
}

void FAsyncReprojectionViewExtension::PostRenderViewFamily_RenderThread(FRDGBuilder& GraphBuilder, FSceneViewFamily& InViewFamily)
{
	const FAsyncReprojectionCVarState CVarState = FAsyncReprojectionCVars::Get();
	if (CVarState.WarpPoint != EAsyncReprojectionWarpPoint::PostRenderViewFamily)
	{
		return;
	}

	FRDGTextureRef ViewFamilyTexture = nullptr;
	TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUB = nullptr;
	uint64 CaptureFrameCounter = 0;
	{
		FRWScopeLock Lock(ViewFamilyCaptureLock, SLT_ReadOnly);
		ViewFamilyTexture = ViewFamilyCaptureRT.ViewFamilyTexture;
		SceneTexturesUB = ViewFamilyCaptureRT.SceneTexturesUniformBuffer;
		CaptureFrameCounter = ViewFamilyCaptureRT.FrameCounter;
	}

	if (ViewFamilyTexture == nullptr || CaptureFrameCounter != GFrameCounterRenderThread)
	{
		return;
	}

	const FRDGTextureDesc Desc = ViewFamilyTexture->Desc;

	FRDGTextureRef Temp = GraphBuilder.CreateTexture(Desc, TEXT("AsyncReprojection.ViewFamilyTemp"));
	AddCopyTexturePass(GraphBuilder, ViewFamilyTexture, Temp);

	for (const FSceneView* ViewPtr : InViewFamily.Views)
	{
		if (ViewPtr == nullptr)
		{
			continue;
		}

		const FSceneView& View = *ViewPtr;
		if (!ShouldRunForView(View))
		{
			continue;
		}

		const FIntRect ViewRect = View.UnconstrainedViewRect.IsEmpty()
			? FIntRect(FIntPoint::ZeroValue, Desc.Extent)
			: View.UnconstrainedViewRect;

		FScreenPassTexture Input(Temp, ViewRect);
		FScreenPassRenderTarget Output(ViewFamilyTexture, ViewRect, ERenderTargetLoadAction::ELoad);

		const int32 PlayerIndex = View.PlayerIndex;
	const FAsyncReprojectionCameraSnapshot LatestCamera = FAsyncReprojectionCameraTracker::Get().GetLatestCamera(PlayerIndex);
	if (!LatestCamera.bIsValid)
	{
		if ((GFrameCounterRenderThread - AsyncReprojectionViewExtensionPrivate::LastMissingCameraWarnFrame) >= AsyncReprojectionViewExtensionPrivate::VerboseLogFrameInterval)
		{
			UE_LOG(LogAsyncReprojection, Warning, TEXT("PostRenderViewFamily warp skipped: latest camera is invalid for PlayerIndex=%d."), PlayerIndex);
			AsyncReprojectionViewExtensionPrivate::LastMissingCameraWarnFrame = GFrameCounterRenderThread;
		}

		if (CVarState.bDebugOverlay)
		{
				FAsyncReprojectionOverlayData Overlay;
				Overlay.ModeString = (CVarState.Mode == EAsyncReprojectionMode::Off) ? TEXT("Off") : (CVarState.Mode == EAsyncReprojectionMode::On) ? TEXT("On") : TEXT("Auto");
				Overlay.WarpPointString = TEXT("PostRenderViewFamily");
				Overlay.bActive = false;
				Overlay.FPS = FAsyncReprojectionCameraTracker::Get().GetTrackedFPS();
				Overlay.RefreshHz = FAsyncReprojectionCameraTracker::Get().GetTrackedRefreshHz();
				AsyncReprojectionDebugOverlay::AddOverlayPass(GraphBuilder, View, Output, Overlay);
			}
			continue;
		}

		if (!CVarState.bEnableRotationWarp && !CVarState.bEnableTranslationWarp)
		{
			if (CVarState.bDebugOverlay)
			{
				FAsyncReprojectionOverlayData Overlay;
				Overlay.ModeString = (CVarState.Mode == EAsyncReprojectionMode::Off) ? TEXT("Off") : (CVarState.Mode == EAsyncReprojectionMode::On) ? TEXT("On") : TEXT("Auto");
				Overlay.WarpPointString = TEXT("PostRenderViewFamily");
				Overlay.bActive = false;
				Overlay.FPS = FAsyncReprojectionCameraTracker::Get().GetTrackedFPS();
				Overlay.RefreshHz = FAsyncReprojectionCameraTracker::Get().GetTrackedRefreshHz();
				Overlay.bDepthAvailable = (SceneTexturesUB != nullptr);
				AsyncReprojectionDebugOverlay::AddOverlayPass(GraphBuilder, View, Output, Overlay);
			}
			continue;
		}

		const float FPS = FAsyncReprojectionCameraTracker::Get().GetTrackedFPS();
		const float FPSStdDev = FAsyncReprojectionCameraTracker::Get().GetTrackedFPSStdDev();
		const float RefreshHz = FAsyncReprojectionCameraTracker::Get().GetTrackedRefreshHz();

		const FQuat RenderedRotation = View.ViewRotation.Quaternion();
		const FVector RenderedLocation = View.ViewLocation;

		const FQuat LatestRotation = LatestCamera.CameraTransform.GetRotation();
		const FVector LatestLocation = LatestCamera.CameraTransform.GetLocation();

		FQuat RawDeltaQuat = LatestRotation * RenderedRotation.Inverse();
		FRotator RawDeltaRot = RawDeltaQuat.Rotator();
		FVector RawDeltaTranslation = LatestLocation - RenderedLocation;

		const FAsyncReprojectionRenderedViewSnapshot RenderedViewSnapshot = FAsyncReprojectionCameraTracker::Get().GetLatestRenderedView_RenderThread(PlayerIndex);
		const FQuat InputDeltaQuat = ComputeInputDrivenDeltaQuat(CVarState, PlayerIndex, RenderedRotation, RenderedViewSnapshot);
		RawDeltaQuat = InputDeltaQuat * RawDeltaQuat;
		RawDeltaRot = RawDeltaQuat.Rotator();

		if (!CVarState.bEnableRotationWarp)
		{
			RawDeltaQuat = FQuat::Identity;
			RawDeltaRot = FRotator::ZeroRotator;
		}
		if (!CVarState.bEnableTranslationWarp)
		{
			RawDeltaTranslation = FVector::ZeroVector;
		}

		const float RawTranslationMag = RawDeltaTranslation.Size();

		const float AbsYaw = FMath::Abs(RawDeltaRot.Yaw);
			const float AbsPitch = FMath::Abs(RawDeltaRot.Pitch);
			const float AbsRoll = FMath::Abs(RawDeltaRot.Roll);
			const float MaxRot = FMath::Max3(AbsYaw, AbsPitch, AbsRoll);

			const bool bDepthAvailable = SceneTexturesUB != nullptr;
			const bool bTranslationEnabled = CVarState.bEnableTranslationWarp && (!CVarState.bRequireDepthForTranslation || bDepthAvailable);

			const float MaxRotClamp = FMath::Max3(CVarState.MaxYawDegreesPerFrame, CVarState.MaxPitchDegreesPerFrame, CVarState.MaxRollDegreesPerFrame);
			const float MaxTransClamp = CVarState.MaxTranslationCmPerFrame;

			float Weight = 1.0f;
		if (MaxRotClamp > 0.0f)
		{
			Weight *= FMath::Clamp(1.0f - FMath::Max(0.0f, (MaxRot - MaxRotClamp) / (0.5f * MaxRotClamp + 1e-3f)), 0.0f, 1.0f);
		}
		if (MaxTransClamp > 0.0f)
		{
			Weight *= FMath::Clamp(1.0f - FMath::Max(0.0f, (RawTranslationMag - MaxTransClamp) / (0.5f * MaxTransClamp + 1e-3f)), 0.0f, 1.0f);
		}

		bool bActive = (CVarState.Mode == EAsyncReprojectionMode::On);
		if (CVarState.Mode == EAsyncReprojectionMode::Auto)
		{
			const bool bHasRefresh = RefreshHz > 1.0f;
			const bool bRefreshDeltaOk = bHasRefresh && (RefreshHz - FPS) >= CVarState.AutoMinRefreshDeltaHz;
			const bool bStable = FPSStdDev <= CVarState.AutoMaxFPSStdDev;
			const bool bWarpSmall = (MaxRot <= CVarState.AutoMaxWarpDegrees) && (RawTranslationMag <= CVarState.AutoMaxTranslationCm);
			bActive = bRefreshDeltaOk && bStable && bWarpSmall;
		}
		if (!bActive || Weight <= 0.0f)
		{
			if (CVarState.bDebugOverlay)
			{
				FAsyncReprojectionOverlayData Overlay;
				Overlay.ModeString = (CVarState.Mode == EAsyncReprojectionMode::Off) ? TEXT("Off") : (CVarState.Mode == EAsyncReprojectionMode::On) ? TEXT("On") : TEXT("Auto");
				Overlay.WarpPointString = TEXT("PostRenderViewFamily");
				Overlay.bActive = bActive;
				Overlay.FPS = FPS;
					Overlay.RefreshHz = RefreshHz;
					Overlay.DeltaRotDegrees = RawDeltaRot;
					Overlay.DeltaTransCm = RawTranslationMag;
					Overlay.bDepthAvailable = bDepthAvailable;
					Overlay.bTranslationEnabled = bTranslationEnabled;
					Overlay.Weight = Weight;
					AsyncReprojectionDebugOverlay::AddOverlayPass(GraphBuilder, View, Output, Overlay);
				}
				continue;
			}

			FLatchedWarpState& Latch = LatchedWarpByPlayer.FindOrAdd(PlayerIndex);
		if (!CVarState.bDebugFreezeWarp || !Latch.bValid)
		{
			FRotator ClampedRot = RawDeltaRot;
			ClampedRot.Yaw = FMath::Clamp(ClampedRot.Yaw, -CVarState.MaxYawDegreesPerFrame, CVarState.MaxYawDegreesPerFrame);
			ClampedRot.Pitch = FMath::Clamp(ClampedRot.Pitch, -CVarState.MaxPitchDegreesPerFrame, CVarState.MaxPitchDegreesPerFrame);
			ClampedRot.Roll = FMath::Clamp(ClampedRot.Roll, -CVarState.MaxRollDegreesPerFrame, CVarState.MaxRollDegreesPerFrame);

			const FVector ClampedTranslation = (MaxTransClamp > 0.0f) ? RawDeltaTranslation.GetClampedToMaxSize(MaxTransClamp) : RawDeltaTranslation;

			Latch.bValid = true;
			Latch.DeltaQuat = ClampedRot.Quaternion();
			Latch.DeltaTranslationCm = ClampedTranslation;
			Latch.Weight = Weight;
			Latch.bDepthAvailable = bDepthAvailable;
			Latch.bTranslationEnabled = bTranslationEnabled;
		}

		const FQuat UsedDeltaQuat = Latch.DeltaQuat;
		const FVector UsedDeltaTranslation = Latch.DeltaTranslationCm;
		const float UsedWeight = Latch.Weight;

		FAsyncReprojectionWarpPassInputs WarpInputs;
		WarpInputs.SceneColor = Input;
		WarpInputs.Output = Output;
		WarpInputs.SceneTexturesUniformBuffer = Latch.bTranslationEnabled
			? TRDGUniformBufferBinding<FSceneTextureUniformParameters>(SceneTexturesUB)
			: TRDGUniformBufferBinding<FSceneTextureUniformParameters>();
		WarpInputs.WarpWeight = UsedWeight;

		const FVector PreViewTranslation = View.ViewMatrices.GetPreViewTranslation();
		const FVector ClampedLatestLocation = RenderedLocation + UsedDeltaTranslation;
		const FQuat ClampedLatestRotation = UsedDeltaQuat * RenderedRotation;

		const FVector LatestOriginPlusPreView = ClampedLatestLocation + PreViewTranslation;

		const FMatrix LatestWorldToViewRotation = FInverseRotationMatrix(ClampedLatestRotation.Rotator());
		const FMatrix LatestTranslatedWorldToView = LatestWorldToViewRotation * FTranslationMatrix(-LatestOriginPlusPreView);
		const FMatrix LatestTranslatedWorldToClip = LatestTranslatedWorldToView * View.ViewMatrices.GetProjectionMatrix();

		WarpInputs.TranslatedWorldToLatestClip = FMatrix44f(LatestTranslatedWorldToClip);
		WarpInputs.DeltaRotationInv4x4 = FMatrix44f(FQuatRotationMatrix(UsedDeltaQuat.Inverse()));
		WarpInputs.bEnableTranslation = Latch.bTranslationEnabled;

		const double CpuStart = FPlatformTime::Seconds();
		AsyncReprojectionWarp::AddWarpPass(GraphBuilder, View, WarpInputs);
		const double CpuEnd = FPlatformTime::Seconds();

		if (CVarState.bDebugOverlay)
		{
			FAsyncReprojectionOverlayData Overlay;
			Overlay.ModeString = (CVarState.Mode == EAsyncReprojectionMode::Off) ? TEXT("Off") : (CVarState.Mode == EAsyncReprojectionMode::On) ? TEXT("On") : TEXT("Auto");
			Overlay.WarpPointString = TEXT("PostRenderViewFamily");
			Overlay.bActive = bActive;
			Overlay.FPS = FPS;
			Overlay.RefreshHz = RefreshHz;
			Overlay.DeltaRotDegrees = UsedDeltaQuat.Rotator();
			Overlay.DeltaTransCm = UsedDeltaTranslation.Size();
			Overlay.bDepthAvailable = bDepthAvailable;
			Overlay.bTranslationEnabled = WarpInputs.bEnableTranslation;
			Overlay.Weight = UsedWeight;
			Overlay.CpuSubmitMs = float((CpuEnd - CpuStart) * 1000.0);
			AsyncReprojectionDebugOverlay::AddOverlayPass(GraphBuilder, View, Output, Overlay);
		}

		FAsyncReprojectionDeltaSnapshot DeltaSnapshot;
		DeltaSnapshot.DeltaRotationDegrees = UsedDeltaQuat.Rotator();
		DeltaSnapshot.DeltaTranslationCm = UsedDeltaTranslation;
		DeltaSnapshot.bDepthAvailable = bDepthAvailable;
		DeltaSnapshot.bTranslationEnabled = WarpInputs.bEnableTranslation;
		DeltaSnapshot.StrengthWeight = UsedWeight;
		FAsyncReprojectionCameraTracker::Get().PublishDelta_RenderThread(PlayerIndex, DeltaSnapshot);
	}
}

void FAsyncReprojectionViewExtension::SubscribeToPostProcessingPass(
	EPostProcessingPass Pass,
	const FSceneView& InView,
	FAfterPassCallbackDelegateArray& InOutPassCallbacks,
	bool bIsPassEnabled)
{
	const FAsyncReprojectionCVarState CVarState = FAsyncReprojectionCVars::Get();
	const bool bAsyncPipelineEnabled = CVarState.bAsyncPresent || CVarState.TimewarpMode != EAsyncReprojectionTimewarpMode::FullRender;
	const bool bWantsAfterPassCallback = (CVarState.WarpPoint == EAsyncReprojectionWarpPoint::EndOfPostProcess) || bAsyncPipelineEnabled;
	if (!bWantsAfterPassCallback)
	{
		return;
	}

	if (LastAfterPassFrameCounter != GFrameCounterRenderThread)
	{
		LastAfterPassFrameCounter = GFrameCounterRenderThread;
		LastAfterPassByPlayer.Reset();
	}

	if (!bIsPassEnabled)
	{
		return;
	}

	const bool bCandidate =
		Pass == EPostProcessingPass::ReplacingTonemapper ||
		Pass == EPostProcessingPass::MotionBlur ||
		Pass == EPostProcessingPass::Tonemap ||
		Pass == EPostProcessingPass::FXAA ||
		Pass == EPostProcessingPass::VisualizeDepthOfField;

	if (bCandidate)
	{
		LastAfterPassByPlayer.Add(InView.PlayerIndex, Pass);
		InOutPassCallbacks.Add(FAfterPassCallbackDelegate::CreateRaw(this, &FAsyncReprojectionViewExtension::PostProcessPass_RenderThread, Pass));
	}
}

static const TCHAR* PostProcessPassToString(ISceneViewExtension::EPostProcessingPass PassId)
{
	switch (PassId)
	{
	case ISceneViewExtension::EPostProcessingPass::ReplacingTonemapper: return TEXT("ReplacingTonemapper");
	case ISceneViewExtension::EPostProcessingPass::MotionBlur: return TEXT("MotionBlur");
	case ISceneViewExtension::EPostProcessingPass::Tonemap: return TEXT("Tonemap");
	case ISceneViewExtension::EPostProcessingPass::FXAA: return TEXT("FXAA");
	case ISceneViewExtension::EPostProcessingPass::VisualizeDepthOfField: return TEXT("VisualizeDOF");
	default: return TEXT("Other");
	}
}

FScreenPassTexture FAsyncReprojectionViewExtension::PostProcessPass_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessMaterialInputs& Inputs, EPostProcessingPass PassId)
{
	const FAsyncReprojectionCVarState CVarState = FAsyncReprojectionCVars::Get();
	if (!ShouldRunForView(View))
	{
		return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
	}

	const int32 PlayerIndex = View.PlayerIndex;
	if (const EPostProcessingPass* LastPass = LastAfterPassByPlayer.Find(PlayerIndex))
	{
		if (*LastPass != PassId)
		{
			return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
		}
	}
	else
	{
		return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
	}

	const bool bAsyncPipelineEnabled = CVarState.bAsyncPresent || CVarState.TimewarpMode != EAsyncReprojectionTimewarpMode::FullRender;
	if (bAsyncPipelineEnabled)
	{
		FAsyncReprojectionFrameCache::Get().Update_RenderThread(GraphBuilder, View, Inputs);
	}

	if (CVarState.WarpPoint != EAsyncReprojectionWarpPoint::EndOfPostProcess)
	{
		return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
	}

		auto ReturnWithOverlay = [&](FAsyncReprojectionOverlayData Overlay) -> FScreenPassTexture
		{
			if (!CVarState.bDebugOverlay)
			{
				return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
			}
			
			Overlay.bAsyncPresentEnabled = bAsyncPipelineEnabled;

			FScreenPassTexture SceneColor = Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
			const FScreenPassRenderTarget Output = Inputs.OverrideOutput.IsValid()
				? Inputs.OverrideOutput
				: FScreenPassRenderTarget::CreateFromInput(GraphBuilder, SceneColor, View.GetOverwriteLoadAction(), TEXT("AsyncReprojection.DebugOverlayOutput"));

		if (Output.Texture != SceneColor.Texture)
		{
			AddCopyTexturePass(GraphBuilder, SceneColor.Texture, Output.Texture);
		}

		AsyncReprojectionDebugOverlay::AddOverlayPass(GraphBuilder, View, Output, Overlay);
		return FScreenPassTexture(Output);
	};

	if (!CVarState.bEnableRotationWarp && !CVarState.bEnableTranslationWarp)
	{
		FAsyncReprojectionOverlayData Overlay;
		Overlay.ModeString = (CVarState.Mode == EAsyncReprojectionMode::Off) ? TEXT("Off") : (CVarState.Mode == EAsyncReprojectionMode::On) ? TEXT("On") : TEXT("Auto");
		Overlay.WarpPointString = FString::Printf(TEXT("EndOfPostProcess:%s"), PostProcessPassToString(PassId));
		Overlay.bActive = false;
		Overlay.FPS = FAsyncReprojectionCameraTracker::Get().GetTrackedFPS();
		Overlay.RefreshHz = FAsyncReprojectionCameraTracker::Get().GetTrackedRefreshHz();
		return ReturnWithOverlay(Overlay);
	}

	const FAsyncReprojectionCameraSnapshot LatestCamera = FAsyncReprojectionCameraTracker::Get().GetLatestCamera(PlayerIndex);
	if (!LatestCamera.bIsValid)
	{
		if ((GFrameCounterRenderThread - AsyncReprojectionViewExtensionPrivate::LastMissingCameraWarnFrame) >= AsyncReprojectionViewExtensionPrivate::VerboseLogFrameInterval)
		{
			UE_LOG(LogAsyncReprojection, Warning, TEXT("EndOfPostProcess warp skipped: latest camera is invalid for PlayerIndex=%d."), PlayerIndex);
			AsyncReprojectionViewExtensionPrivate::LastMissingCameraWarnFrame = GFrameCounterRenderThread;
		}

		FAsyncReprojectionOverlayData Overlay;
		Overlay.ModeString = (CVarState.Mode == EAsyncReprojectionMode::Off) ? TEXT("Off") : (CVarState.Mode == EAsyncReprojectionMode::On) ? TEXT("On") : TEXT("Auto");
		Overlay.WarpPointString = FString::Printf(TEXT("EndOfPostProcess:%s"), PostProcessPassToString(PassId));
		Overlay.bActive = false;
		Overlay.FPS = FAsyncReprojectionCameraTracker::Get().GetTrackedFPS();
		Overlay.RefreshHz = FAsyncReprojectionCameraTracker::Get().GetTrackedRefreshHz();
		return ReturnWithOverlay(Overlay);
	}

	const float FPS = FAsyncReprojectionCameraTracker::Get().GetTrackedFPS();
	const float FPSStdDev = FAsyncReprojectionCameraTracker::Get().GetTrackedFPSStdDev();
	const float RefreshHz = FAsyncReprojectionCameraTracker::Get().GetTrackedRefreshHz();

	const FQuat RenderedRotation = View.ViewRotation.Quaternion();
	const FVector RenderedLocation = View.ViewLocation;

	const FQuat LatestRotation = LatestCamera.CameraTransform.GetRotation();
	const FVector LatestLocation = LatestCamera.CameraTransform.GetLocation();

	FQuat RawDeltaQuat = LatestRotation * RenderedRotation.Inverse();
	FRotator RawDeltaRot = RawDeltaQuat.Rotator();
	FVector RawDeltaTranslation = LatestLocation - RenderedLocation;

	const FAsyncReprojectionRenderedViewSnapshot RenderedViewSnapshot = FAsyncReprojectionCameraTracker::Get().GetLatestRenderedView_RenderThread(PlayerIndex);
	const FQuat InputDeltaQuat = ComputeInputDrivenDeltaQuat(CVarState, PlayerIndex, RenderedRotation, RenderedViewSnapshot);
	RawDeltaQuat = InputDeltaQuat * RawDeltaQuat;
	RawDeltaRot = RawDeltaQuat.Rotator();

	if (!CVarState.bEnableRotationWarp)
	{
		RawDeltaQuat = FQuat::Identity;
		RawDeltaRot = FRotator::ZeroRotator;
	}
	if (!CVarState.bEnableTranslationWarp)
	{
		RawDeltaTranslation = FVector::ZeroVector;
	}

	const float AbsYaw = FMath::Abs(RawDeltaRot.Yaw);
	const float AbsPitch = FMath::Abs(RawDeltaRot.Pitch);
	const float AbsRoll = FMath::Abs(RawDeltaRot.Roll);
	const float TranslationMag = RawDeltaTranslation.Size();

	const bool bDepthAvailable = bool(Inputs.SceneTextures.SceneTextures);

	const bool bEnableTranslation = CVarState.bEnableTranslationWarp && (!CVarState.bRequireDepthForTranslation || bDepthAvailable);

	float Weight = 1.0f;
	const float MaxRot = FMath::Max3(AbsYaw, AbsPitch, AbsRoll);
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

	bool bActive = (CVarState.Mode == EAsyncReprojectionMode::On);
	if (CVarState.Mode == EAsyncReprojectionMode::Auto)
	{
		const bool bHasRefresh = RefreshHz > 1.0f;
		const bool bRefreshDeltaOk = bHasRefresh && (RefreshHz - FPS) >= CVarState.AutoMinRefreshDeltaHz;
		const bool bStable = FPSStdDev <= CVarState.AutoMaxFPSStdDev;
		const bool bWarpSmall = (MaxRot <= CVarState.AutoMaxWarpDegrees) && (TranslationMag <= CVarState.AutoMaxTranslationCm);
		bActive = bRefreshDeltaOk && bStable && bWarpSmall;
	}

	if (!bActive || Weight <= 0.0f)
	{
		FAsyncReprojectionOverlayData Overlay;
		Overlay.ModeString = (CVarState.Mode == EAsyncReprojectionMode::Off) ? TEXT("Off") : (CVarState.Mode == EAsyncReprojectionMode::On) ? TEXT("On") : TEXT("Auto");
		Overlay.WarpPointString = FString::Printf(TEXT("EndOfPostProcess:%s"), PostProcessPassToString(PassId));
		Overlay.bActive = bActive;
		Overlay.FPS = FPS;
		Overlay.RefreshHz = RefreshHz;
		Overlay.DeltaRotDegrees = RawDeltaRot;
		Overlay.DeltaTransCm = TranslationMag;
		Overlay.bDepthAvailable = bDepthAvailable;
		Overlay.bTranslationEnabled = bEnableTranslation;
		Overlay.Weight = Weight;
		return ReturnWithOverlay(Overlay);
	}

	FLatchedWarpState& Latch = LatchedWarpByPlayer.FindOrAdd(PlayerIndex);
	if (!CVarState.bDebugFreezeWarp || !Latch.bValid)
	{
		FRotator ClampedRot = RawDeltaRot;
		ClampedRot.Yaw = FMath::Clamp(ClampedRot.Yaw, -CVarState.MaxYawDegreesPerFrame, CVarState.MaxYawDegreesPerFrame);
		ClampedRot.Pitch = FMath::Clamp(ClampedRot.Pitch, -CVarState.MaxPitchDegreesPerFrame, CVarState.MaxPitchDegreesPerFrame);
		ClampedRot.Roll = FMath::Clamp(ClampedRot.Roll, -CVarState.MaxRollDegreesPerFrame, CVarState.MaxRollDegreesPerFrame);

		const FVector ClampedTranslation = (CVarState.MaxTranslationCmPerFrame > 0.0f) ? RawDeltaTranslation.GetClampedToMaxSize(CVarState.MaxTranslationCmPerFrame) : RawDeltaTranslation;

		Latch.bValid = true;
		Latch.DeltaQuat = ClampedRot.Quaternion();
		Latch.DeltaTranslationCm = ClampedTranslation;
		Latch.Weight = Weight;
		Latch.bDepthAvailable = bDepthAvailable;
		Latch.bTranslationEnabled = bEnableTranslation;
	}

	const FQuat UsedDeltaQuat = Latch.DeltaQuat;
	const FVector UsedDeltaTranslation = Latch.DeltaTranslationCm;
	const float UsedWeight = Latch.Weight;

	FScreenPassTexture SceneColor = Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);

	const FScreenPassRenderTarget Output = Inputs.OverrideOutput.IsValid()
		? Inputs.OverrideOutput
		: FScreenPassRenderTarget::CreateFromInput(GraphBuilder, SceneColor, View.GetOverwriteLoadAction(), TEXT("AsyncReprojection.SceneColor"));

	FAsyncReprojectionWarpPassInputs WarpInputs;
	WarpInputs.SceneColor = SceneColor;
	WarpInputs.Output = Output;
	WarpInputs.SceneTexturesUniformBuffer = Latch.bTranslationEnabled
		? Inputs.SceneTextures.SceneTextures
		: TRDGUniformBufferBinding<FSceneTextureUniformParameters>();
	WarpInputs.WarpWeight = UsedWeight;

	const FVector PreViewTranslation = View.ViewMatrices.GetPreViewTranslation();
	const FVector ClampedLatestLocation = RenderedLocation + UsedDeltaTranslation;
	const FQuat ClampedLatestRotation = UsedDeltaQuat * RenderedRotation;

	const FVector LatestOriginPlusPreView = ClampedLatestLocation + PreViewTranslation;

	const FMatrix LatestWorldToViewRotation = FInverseRotationMatrix(ClampedLatestRotation.Rotator());
	const FMatrix LatestTranslatedWorldToView = LatestWorldToViewRotation * FTranslationMatrix(-LatestOriginPlusPreView);
	const FMatrix LatestTranslatedWorldToClip = LatestTranslatedWorldToView * View.ViewMatrices.GetProjectionMatrix();

	WarpInputs.TranslatedWorldToLatestClip = FMatrix44f(LatestTranslatedWorldToClip);
	WarpInputs.DeltaRotationInv4x4 = FMatrix44f(FQuatRotationMatrix(UsedDeltaQuat.Inverse()));
	WarpInputs.bEnableTranslation = Latch.bTranslationEnabled;

	const double CpuStart = FPlatformTime::Seconds();
	FScreenPassTexture Result = AsyncReprojectionWarp::AddWarpPass(GraphBuilder, View, WarpInputs);
	const double CpuEnd = FPlatformTime::Seconds();

	if (CVarState.bDebugOverlay)
	{
		FAsyncReprojectionOverlayData Overlay;
		Overlay.ModeString = (CVarState.Mode == EAsyncReprojectionMode::Off) ? TEXT("Off") : (CVarState.Mode == EAsyncReprojectionMode::On) ? TEXT("On") : TEXT("Auto");
		Overlay.WarpPointString = FString::Printf(TEXT("EndOfPostProcess:%s"), PostProcessPassToString(PassId));
		Overlay.bActive = bActive;
		Overlay.FPS = FPS;
		Overlay.RefreshHz = RefreshHz;
		Overlay.DeltaRotDegrees = UsedDeltaQuat.Rotator();
		Overlay.DeltaTransCm = UsedDeltaTranslation.Size();
		Overlay.bDepthAvailable = bDepthAvailable;
		Overlay.bTranslationEnabled = WarpInputs.bEnableTranslation;
		Overlay.Weight = UsedWeight;
		Overlay.CpuSubmitMs = float((CpuEnd - CpuStart) * 1000.0);
		AsyncReprojectionDebugOverlay::AddOverlayPass(GraphBuilder, View, Output, Overlay);
	}

	FAsyncReprojectionDeltaSnapshot DeltaSnapshot;
	DeltaSnapshot.DeltaRotationDegrees = UsedDeltaQuat.Rotator();
	DeltaSnapshot.DeltaTranslationCm = UsedDeltaTranslation;
	DeltaSnapshot.bDepthAvailable = bDepthAvailable;
	DeltaSnapshot.bTranslationEnabled = WarpInputs.bEnableTranslation;
	DeltaSnapshot.StrengthWeight = UsedWeight;
	FAsyncReprojectionCameraTracker::Get().PublishDelta_RenderThread(PlayerIndex, DeltaSnapshot);

	return Result;
}

bool FAsyncReprojectionViewExtension::ShouldRunForView(const FSceneView& View) const
{
	const FAsyncReprojectionCVarState CVarState = FAsyncReprojectionCVars::Get();

	if (!View.bIsGameView)
	{
		if ((GFrameCounterRenderThread - AsyncReprojectionViewExtensionPrivate::LastShouldRunSkipLogFrame) >= AsyncReprojectionViewExtensionPrivate::VerboseLogFrameInterval)
		{
			UE_LOG(LogAsyncReprojection, VeryVerbose, TEXT("ShouldRunForView=false: non-game view."));
			AsyncReprojectionViewExtensionPrivate::LastShouldRunSkipLogFrame = GFrameCounterRenderThread;
		}
		return false;
	}

	if (View.bIsSceneCapture || View.bIsReflectionCapture || View.bIsPlanarReflection)
	{
		if ((GFrameCounterRenderThread - AsyncReprojectionViewExtensionPrivate::LastShouldRunSkipLogFrame) >= AsyncReprojectionViewExtensionPrivate::VerboseLogFrameInterval)
		{
			UE_LOG(LogAsyncReprojection, VeryVerbose, TEXT("ShouldRunForView=false: capture/reflection view."));
			AsyncReprojectionViewExtensionPrivate::LastShouldRunSkipLogFrame = GFrameCounterRenderThread;
		}
		return false;
	}

	if (!View.IsPerspectiveProjection())
	{
		if ((GFrameCounterRenderThread - AsyncReprojectionViewExtensionPrivate::LastShouldRunSkipLogFrame) >= AsyncReprojectionViewExtensionPrivate::VerboseLogFrameInterval)
		{
			UE_LOG(LogAsyncReprojection, VeryVerbose, TEXT("ShouldRunForView=false: non-perspective projection."));
			AsyncReprojectionViewExtensionPrivate::LastShouldRunSkipLogFrame = GFrameCounterRenderThread;
		}
		return false;
	}

	if (!CVarState.bEnableInEditor)
	{
		if (GIsEditor)
		{
			if ((GFrameCounterRenderThread - AsyncReprojectionViewExtensionPrivate::LastShouldRunSkipLogFrame) >= AsyncReprojectionViewExtensionPrivate::VerboseLogFrameInterval)
			{
				UE_LOG(LogAsyncReprojection, VeryVerbose, TEXT("ShouldRunForView=false: editor viewport disabled by CVar."));
				AsyncReprojectionViewExtensionPrivate::LastShouldRunSkipLogFrame = GFrameCounterRenderThread;
			}
			return false;
		}
	}

	return true;
}
