// Copyright © 2023–2026 Segritude Ltd. All Rights Reserved.

#include "AsyncReprojectionBlueprintLibrary.h"

#include "AsyncReprojectionCameraTracker.h"

void UAsyncReprojectionBlueprintLibrary::GetAsyncReprojectionLatestCameraTransform(const UObject* WorldContextObject, int32 PlayerIndex, FTransform& OutTransform, bool& bOutValid)
{
	bOutValid = false;
	OutTransform = FTransform::Identity;

	if (WorldContextObject == nullptr)
	{
		return;
	}

	const FAsyncReprojectionCameraSnapshot Snapshot = FAsyncReprojectionCameraTracker::Get().GetLatestCamera(PlayerIndex);
	if (!Snapshot.bIsValid)
	{
		return;
	}

	OutTransform = Snapshot.CameraTransform;
	bOutValid = true;
}

void UAsyncReprojectionBlueprintLibrary::GetAsyncReprojectionDelta(const UObject* WorldContextObject, int32 PlayerIndex, FAsyncReprojectionDelta& OutDelta)
{
	OutDelta = FAsyncReprojectionDelta();

	if (WorldContextObject == nullptr)
	{
		return;
	}

	const FAsyncReprojectionDeltaSnapshot Snapshot = FAsyncReprojectionCameraTracker::Get().GetLatestDelta(PlayerIndex);
	OutDelta.DeltaRotationDegrees = Snapshot.DeltaRotationDegrees;
	OutDelta.DeltaTranslationCm = Snapshot.DeltaTranslationCm;
	OutDelta.bDepthAvailable = Snapshot.bDepthAvailable;
	OutDelta.bTranslationEnabled = Snapshot.bTranslationEnabled;
	OutDelta.StrengthWeight = Snapshot.StrengthWeight;
}

void UAsyncReprojectionBlueprintLibrary::SubmitAsyncReprojectionLatestCameraTransform(const UObject* WorldContextObject, int32 PlayerIndex, const FTransform& CameraTransform)
{
	if (WorldContextObject == nullptr)
	{
		return;
	}

	FAsyncReprojectionCameraTracker::Get().SubmitLatestCameraTransform_GameThread(PlayerIndex, CameraTransform);
}
