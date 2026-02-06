// Copyright © 2023–2026 Segritude Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AsyncReprojectionTypes.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "AsyncReprojectionBlueprintLibrary.generated.h"

/**
 * @class UAsyncReprojectionBlueprintLibrary
 *
 * Blueprint helpers for querying the latest camera transform and current warp delta.
 */
UCLASS()
class ASYNCREPROJECTION_API UAsyncReprojectionBlueprintLibrary final : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Returns the most recent camera transform sampled by AsyncReprojection (game-thread snapshot).
	 *
	 * @param WorldContextObject World context.
	 * @param PlayerIndex Local player index.
	 * @param OutTransform Latest camera transform.
	 * @param bOutValid True when a valid snapshot is available.
	 */
	UFUNCTION(BlueprintCallable, Category = "AsyncReprojection", meta = (WorldContext = "WorldContextObject"))
	static void GetAsyncReprojectionLatestCameraTransform(const UObject* WorldContextObject, int32 PlayerIndex, FTransform& OutTransform, bool& bOutValid);

	/**
	 * Returns the most recent warp delta published from the render thread.
	 *
	 * @param WorldContextObject World context.
	 * @param PlayerIndex Local player index.
	 * @param OutDelta Latest delta snapshot.
	 */
	UFUNCTION(BlueprintCallable, Category = "AsyncReprojection", meta = (WorldContext = "WorldContextObject"))
	static void GetAsyncReprojectionDelta(const UObject* WorldContextObject, int32 PlayerIndex, FAsyncReprojectionDelta& OutDelta);

	/**
	 * Submits an explicit latest camera transform for the current frame.
	 *
	 * @param WorldContextObject World context.
	 * @param PlayerIndex Local player index.
	 * @param CameraTransform Transform to publish as the latest camera.
	 */
	UFUNCTION(BlueprintCallable, Category = "AsyncReprojection", meta = (WorldContext = "WorldContextObject"))
	static void SubmitAsyncReprojectionLatestCameraTransform(const UObject* WorldContextObject, int32 PlayerIndex, const FTransform& CameraTransform);
};
