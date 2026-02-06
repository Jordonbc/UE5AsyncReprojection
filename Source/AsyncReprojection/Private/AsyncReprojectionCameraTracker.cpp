// Copyright © 2023–2026 Segritude Ltd. All Rights Reserved.

#include "AsyncReprojectionCameraTracker.h"

#include "AsyncReprojectionCVars.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CoreDelegates.h"

namespace AsyncReprojectionAtomic
{
	static void FloatFetchAdd(std::atomic<float>& Value, float ToAdd)
	{
		float Old = Value.load(std::memory_order_relaxed);
		while (!Value.compare_exchange_weak(Old, Old + ToAdd, std::memory_order_relaxed))
		{
		}
	}
}

static float GetBestEffortRefreshHz(float RefreshHzOverride)
{
	if (RefreshHzOverride > 1.0f)
	{
		return RefreshHzOverride;
	}

	static const IConsoleVariable* TargetRefreshRateCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("RHI.TargetRefreshRate"));
	if (TargetRefreshRateCVar != nullptr)
	{
		const int32 TargetRefreshRate = TargetRefreshRateCVar->GetInt();
		if (TargetRefreshRate > 0)
		{
			return static_cast<float>(TargetRefreshRate);
		}
	}

	return 0.0f;
}

FAsyncReprojectionCameraTracker& FAsyncReprojectionCameraTracker::Get()
{
	static FAsyncReprojectionCameraTracker Instance;
	return Instance;
}

FAsyncReprojectionCameraTracker::FAsyncReprojectionCameraTracker()
{
	for (int32 Index = 0; Index < MaxTrackedPlayers; Index++)
	{
		ExternalCameraSubmitFrameCounter[Index].Store(0, EMemoryOrder::Relaxed);
		MouseXTotal[Index].store(0.0f, std::memory_order_relaxed);
		MouseYTotal[Index].store(0.0f, std::memory_order_relaxed);
	}
}

FAsyncReprojectionCameraTracker::~FAsyncReprojectionCameraTracker()
{
	Shutdown();
}

void FAsyncReprojectionCameraTracker::Startup()
{
	const bool bWasStarted = bStarted.Exchange(true);
	if (bWasStarted)
	{
		return;
	}

	EndFrameHandle = FCoreDelegates::OnEndFrame.AddRaw(this, &FAsyncReprojectionCameraTracker::OnEndFrame_GameThread);
}

void FAsyncReprojectionCameraTracker::Shutdown()
{
	const bool bWasStarted = bStarted.Exchange(false);
	if (!bWasStarted)
	{
		return;
	}

	if (EndFrameHandle.IsValid())
	{
		FCoreDelegates::OnEndFrame.Remove(EndFrameHandle);
		EndFrameHandle = FDelegateHandle();
	}

	FpsSamples.Reset();
	TrackedFPS.Store(0.0f);
	TrackedFPSStdDev.Store(0.0f);
	TrackedRefreshHz.Store(0.0f);

	for (int32 Index = 0; Index < MaxTrackedPlayers; Index++)
	{
		ExternalCameraSubmitFrameCounter[Index].Store(0, EMemoryOrder::Relaxed);
		MouseXTotal[Index].store(0.0f, std::memory_order_relaxed);
		MouseYTotal[Index].store(0.0f, std::memory_order_relaxed);
	}
}

FAsyncReprojectionCameraSnapshot FAsyncReprojectionCameraTracker::GetLatestCamera(int32 PlayerIndex) const
{
	if (PlayerIndex < 0 || PlayerIndex >= MaxTrackedPlayers)
	{
		return FAsyncReprojectionCameraSnapshot();
	}

	const FCameraBuffer& Buffer = CameraBuffers[PlayerIndex];
	const uint32 Index = Buffer.WriteIndex.Load(EMemoryOrder::Relaxed) & 1u;
	return Buffer.Snapshots[Index];
}

FAsyncReprojectionDeltaSnapshot FAsyncReprojectionCameraTracker::GetLatestDelta(int32 PlayerIndex) const
{
	if (PlayerIndex < 0 || PlayerIndex >= MaxTrackedPlayers)
	{
		return FAsyncReprojectionDeltaSnapshot();
	}

	const FDeltaBuffer& Buffer = DeltaBuffers[PlayerIndex];
	const uint32 Index = Buffer.WriteIndex.Load(EMemoryOrder::Relaxed) & 1u;
	return Buffer.Snapshots[Index];
}

void FAsyncReprojectionCameraTracker::SubmitLatestCameraTransform_GameThread(int32 PlayerIndex, const FTransform& CameraTransform)
{
	check(IsInGameThread());

	if (PlayerIndex < 0 || PlayerIndex >= MaxTrackedPlayers)
	{
		return;
	}

	FAsyncReprojectionCameraSnapshot Snapshot;
	Snapshot.bIsValid = true;
	Snapshot.TimeSeconds = FPlatformTime::Seconds();
	Snapshot.CameraTransform = CameraTransform;

	FCameraBuffer& Buffer = CameraBuffers[PlayerIndex];
	const uint32 NextIndex = (Buffer.WriteIndex.Load(EMemoryOrder::Relaxed) + 1u) & 1u;
	Buffer.Snapshots[NextIndex] = Snapshot;
	Buffer.WriteIndex.Store(NextIndex, EMemoryOrder::SequentiallyConsistent);

	ExternalCameraSubmitFrameCounter[PlayerIndex].Store(GFrameCounter, EMemoryOrder::Relaxed);
}

void FAsyncReprojectionCameraTracker::AddMouseDelta_GameThread(int32 PlayerIndex, float DeltaX, float DeltaY)
{
	if (PlayerIndex < 0 || PlayerIndex >= MaxTrackedPlayers)
	{
		return;
	}

	AsyncReprojectionAtomic::FloatFetchAdd(MouseXTotal[PlayerIndex], DeltaX);
	AsyncReprojectionAtomic::FloatFetchAdd(MouseYTotal[PlayerIndex], DeltaY);
}

FVector2f FAsyncReprojectionCameraTracker::GetMouseTotals_RenderThread(int32 PlayerIndex) const
{
	if (PlayerIndex < 0 || PlayerIndex >= MaxTrackedPlayers)
	{
		return FVector2f::ZeroVector;
	}

	return FVector2f(
		MouseXTotal[PlayerIndex].load(std::memory_order_relaxed),
		MouseYTotal[PlayerIndex].load(std::memory_order_relaxed));
}

FAsyncReprojectionRenderedViewSnapshot FAsyncReprojectionCameraTracker::GetLatestRenderedView_RenderThread(int32 PlayerIndex) const
{
	if (PlayerIndex < 0 || PlayerIndex >= MaxTrackedPlayers)
	{
		return FAsyncReprojectionRenderedViewSnapshot();
	}

	const FRenderedViewBuffer& Buffer = RenderedViewBuffers[PlayerIndex];
	const uint32 Index = Buffer.WriteIndex.Load(EMemoryOrder::Relaxed) & 1u;
	return Buffer.Snapshots[Index];
}

float FAsyncReprojectionCameraTracker::GetTrackedFPS() const
{
	return TrackedFPS.Load(EMemoryOrder::Relaxed);
}

float FAsyncReprojectionCameraTracker::GetTrackedFPSStdDev() const
{
	return TrackedFPSStdDev.Load(EMemoryOrder::Relaxed);
}

float FAsyncReprojectionCameraTracker::GetTrackedRefreshHz() const
{
	return TrackedRefreshHz.Load(EMemoryOrder::Relaxed);
}

void FAsyncReprojectionCameraTracker::PublishDelta_RenderThread(int32 PlayerIndex, const FAsyncReprojectionDeltaSnapshot& Snapshot)
{
	if (PlayerIndex < 0 || PlayerIndex >= MaxTrackedPlayers)
	{
		return;
	}

	FDeltaBuffer& Buffer = DeltaBuffers[PlayerIndex];
	const uint32 NextIndex = (Buffer.WriteIndex.Load(EMemoryOrder::Relaxed) + 1u) & 1u;
	Buffer.Snapshots[NextIndex] = Snapshot;
	Buffer.WriteIndex.Store(NextIndex, EMemoryOrder::SequentiallyConsistent);
}

void FAsyncReprojectionCameraTracker::PublishRenderedView_RenderThread(int32 PlayerIndex, const FAsyncReprojectionRenderedViewSnapshot& Snapshot)
{
	if (PlayerIndex < 0 || PlayerIndex >= MaxTrackedPlayers)
	{
		return;
	}

	FRenderedViewBuffer& Buffer = RenderedViewBuffers[PlayerIndex];
	const uint32 NextIndex = (Buffer.WriteIndex.Load(EMemoryOrder::Relaxed) + 1u) & 1u;
	Buffer.Snapshots[NextIndex] = Snapshot;
	Buffer.WriteIndex.Store(NextIndex, EMemoryOrder::SequentiallyConsistent);
}

void FAsyncReprojectionCameraTracker::OnEndFrame_GameThread()
{
	const FAsyncReprojectionCVarState CVarState = FAsyncReprojectionCVars::Get();

	const double NowSeconds = FPlatformTime::Seconds();
	const float DeltaSeconds = FApp::GetDeltaTime();

	UpdatePerformance_GameThread(NowSeconds, DeltaSeconds);
	UpdateCameras_GameThread(NowSeconds);

	TrackedRefreshHz.Store(GetBestEffortRefreshHz(CVarState.RefreshHzOverride), EMemoryOrder::Relaxed);
}

void FAsyncReprojectionCameraTracker::UpdatePerformance_GameThread(double NowSeconds, float DeltaSeconds)
{
	if (DeltaSeconds <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	const float FPS = 1.0f / DeltaSeconds;

	FFpsSample Sample;
	Sample.TimeSeconds = NowSeconds;
	Sample.FPS = FPS;
	FpsSamples.Add(Sample);

	const FAsyncReprojectionCVarState CVarState = FAsyncReprojectionCVars::Get();
	const double WindowSeconds = FMath::Max(0.0, static_cast<double>(CVarState.AutoMinStableFPSWindowMs) / 1000.0);
	const double MinTime = NowSeconds - WindowSeconds;

	int32 FirstValidIndex = 0;
	while (FirstValidIndex < FpsSamples.Num() && FpsSamples[FirstValidIndex].TimeSeconds < MinTime)
	{
		FirstValidIndex++;
	}
	if (FirstValidIndex > 0)
	{
		FpsSamples.RemoveAt(0, FirstValidIndex, EAllowShrinking::No);
	}

	float Mean = 0.0f;
	for (const FFpsSample& It : FpsSamples)
	{
		Mean += It.FPS;
	}
	if (FpsSamples.Num() > 0)
	{
		Mean /= static_cast<float>(FpsSamples.Num());
	}

	float Variance = 0.0f;
	for (const FFpsSample& It : FpsSamples)
	{
		const float Diff = It.FPS - Mean;
		Variance += Diff * Diff;
	}
	if (FpsSamples.Num() > 1)
	{
		Variance /= static_cast<float>(FpsSamples.Num() - 1);
	}

	TrackedFPS.Store(Mean, EMemoryOrder::Relaxed);
	TrackedFPSStdDev.Store(FMath::Sqrt(Variance), EMemoryOrder::Relaxed);
}

void FAsyncReprojectionCameraTracker::UpdateCameras_GameThread(double NowSeconds)
{
	if (GEngine == nullptr || GEngine->GameViewport == nullptr)
	{
		return;
	}

	UGameViewportClient* ViewportClient = GEngine->GameViewport;
	UWorld* World = ViewportClient->GetWorld();
	if (World == nullptr)
	{
		return;
	}

	UGameInstance* GameInstance = ViewportClient->GetGameInstance();
	if (GameInstance == nullptr)
	{
		return;
	}

	const TArray<ULocalPlayer*>& LocalPlayers = GameInstance->GetLocalPlayers();
	for (int32 PlayerIndex = 0; PlayerIndex < LocalPlayers.Num() && PlayerIndex < MaxTrackedPlayers; PlayerIndex++)
	{
		const uint64 ExternalSubmitFrame = ExternalCameraSubmitFrameCounter[PlayerIndex].Load(EMemoryOrder::Relaxed);
		if (ExternalSubmitFrame == GFrameCounter)
		{
			continue;
		}

		ULocalPlayer* LocalPlayer = LocalPlayers[PlayerIndex];
		if (LocalPlayer == nullptr)
		{
			continue;
		}

		APlayerController* PC = LocalPlayer->GetPlayerController(World);
		if (PC == nullptr || PC->PlayerCameraManager == nullptr)
		{
			continue;
		}

		FAsyncReprojectionCameraSnapshot Snapshot;
		Snapshot.bIsValid = true;
		Snapshot.TimeSeconds = NowSeconds;
		Snapshot.CameraTransform = FTransform(PC->PlayerCameraManager->GetCameraRotation(), PC->PlayerCameraManager->GetCameraLocation(), FVector::OneVector);

		FCameraBuffer& Buffer = CameraBuffers[PlayerIndex];
		const uint32 NextIndex = (Buffer.WriteIndex.Load(EMemoryOrder::Relaxed) + 1u) & 1u;
		Buffer.Snapshots[NextIndex] = Snapshot;
		Buffer.WriteIndex.Store(NextIndex, EMemoryOrder::SequentiallyConsistent);
	}
}
