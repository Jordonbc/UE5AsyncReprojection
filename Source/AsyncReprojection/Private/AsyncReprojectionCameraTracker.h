// Copyright © 2023–2026 Segritude Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AsyncReprojectionTypes.h"

#include <atomic>

struct FAsyncReprojectionCameraSnapshot
{
	bool bIsValid = false;
	FTransform CameraTransform = FTransform::Identity;
	double TimeSeconds = 0.0;
};

struct FAsyncReprojectionDeltaSnapshot
{
	FRotator DeltaRotationDegrees = FRotator::ZeroRotator;
	FVector DeltaTranslationCm = FVector::ZeroVector;
	bool bDepthAvailable = false;
	bool bTranslationEnabled = false;
	float StrengthWeight = 0.0f;
};

struct FAsyncReprojectionRenderedViewSnapshot
{
	bool bIsValid = false;
	FQuat RenderedRotation = FQuat::Identity;
	FVector RenderedLocation = FVector::ZeroVector;
	float InputMouseXTotal = 0.0f;
	float InputMouseYTotal = 0.0f;
	FMatrix44f ViewToClip = FMatrix44f::Identity;
	FMatrix44f ClipToView = FMatrix44f::Identity;
	FIntRect ViewRect = FIntRect(0, 0, 0, 0);
	ERHIFeatureLevel::Type FeatureLevel = ERHIFeatureLevel::SM5;
};

class FAsyncReprojectionCameraTracker final
{
public:
	static FAsyncReprojectionCameraTracker& Get();

	void Startup();
	void Shutdown();

	FAsyncReprojectionCameraSnapshot GetLatestCamera(int32 PlayerIndex) const;
	FAsyncReprojectionDeltaSnapshot GetLatestDelta(int32 PlayerIndex) const;

	/**
	 * Overrides the current-frame camera snapshot used by AsyncReprojection.
	 *
	 * @param PlayerIndex Local player index.
	 * @param CameraTransform Latest camera transform to use for the current frame.
	 */
	void SubmitLatestCameraTransform_GameThread(int32 PlayerIndex, const FTransform& CameraTransform);

	/**
	 * Accumulates mouse deltas for InputDrivenPose.
	 *
	 * @param PlayerIndex Local player index.
	 * @param DeltaX Mouse delta X in pixels (positive = moved right).
	 * @param DeltaY Mouse delta Y in pixels (positive = moved down).
	 */
	void AddMouseDelta_GameThread(int32 PlayerIndex, float DeltaX, float DeltaY);

	/**
	 * Gets the current mouse totals for InputDrivenPose (render-thread safe).
	 *
	 * @param PlayerIndex Local player index.
	 * @return Mouse totals in pixels (X,Y).
	 */
	FVector2f GetMouseTotals_RenderThread(int32 PlayerIndex) const;

	float GetTrackedFPS() const;
	float GetTrackedFPSStdDev() const;
	float GetTrackedRefreshHz() const;

	void PublishDelta_RenderThread(int32 PlayerIndex, const FAsyncReprojectionDeltaSnapshot& Snapshot);
	void PublishRenderedView_RenderThread(int32 PlayerIndex, const FAsyncReprojectionRenderedViewSnapshot& Snapshot);

	FAsyncReprojectionRenderedViewSnapshot GetLatestRenderedView_RenderThread(int32 PlayerIndex) const;

private:
	FAsyncReprojectionCameraTracker();
	~FAsyncReprojectionCameraTracker();

	void OnEndFrame_GameThread();

	void UpdatePerformance_GameThread(double NowSeconds, float DeltaSeconds);
	void UpdateCameras_GameThread(double NowSeconds);

private:
	struct FCameraBuffer
	{
		TAtomic<uint32> WriteIndex { 0 };
		FAsyncReprojectionCameraSnapshot Snapshots[2];
	};

	struct FDeltaBuffer
	{
		TAtomic<uint32> WriteIndex { 0 };
		FAsyncReprojectionDeltaSnapshot Snapshots[2];
	};

	struct FRenderedViewBuffer
	{
		TAtomic<uint32> WriteIndex { 0 };
		FAsyncReprojectionRenderedViewSnapshot Snapshots[2];
	};

	static constexpr int32 MaxTrackedPlayers = 4;
	FCameraBuffer CameraBuffers[MaxTrackedPlayers];
	FDeltaBuffer DeltaBuffers[MaxTrackedPlayers];
	FRenderedViewBuffer RenderedViewBuffers[MaxTrackedPlayers];
	TAtomic<uint64> ExternalCameraSubmitFrameCounter[MaxTrackedPlayers];
	std::atomic<float> MouseXTotal[MaxTrackedPlayers];
	std::atomic<float> MouseYTotal[MaxTrackedPlayers];

	struct FFpsSample
	{
		double TimeSeconds = 0.0;
		float FPS = 0.0f;
	};

	TArray<FFpsSample> FpsSamples;

	TAtomic<float> TrackedFPS { 0.0f };
	TAtomic<float> TrackedFPSStdDev { 0.0f };
	TAtomic<float> TrackedRefreshHz { 0.0f };

	FDelegateHandle EndFrameHandle;
	TAtomic<bool> bStarted { false };
};
