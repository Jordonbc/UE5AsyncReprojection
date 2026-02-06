// Copyright © 2023–2026 Segritude Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "AsyncReprojectionTypes.generated.h"

/**
 * @enum EAsyncReprojectionMode
 *
 * User-facing mode for AsyncReprojection.
 */
UENUM(BlueprintType)
enum class EAsyncReprojectionMode : uint8
{
	Off UMETA(DisplayName = "Off"),
	On UMETA(DisplayName = "On"),
	Auto UMETA(DisplayName = "Auto"),
};

/**
 * @enum EAsyncReprojectionWarpPoint
 *
 * Where in the render pipeline the warp is applied.
 */
UENUM(BlueprintType)
enum class EAsyncReprojectionWarpPoint : uint8
{
	/**
	 * Warp during the post-processing chain using ISceneViewExtension after-pass callbacks.
	 * Runs before Slate UI composition.
	 */
	EndOfPostProcess UMETA(DisplayName = "End Of Post Process"),

	/**
	 * Warp in PostRenderViewFamily on the view family target.
	 * This is the default and is robust to unusual post-process chains (may require an extra copy).
	 */
	PostRenderViewFamily UMETA(DisplayName = "Post Render View Family"),
};

/**
 * @enum EAsyncReprojectionTimewarpMode
 *
 * Unity-style async timewarp behavior modes.
 */
UENUM(BlueprintType)
enum class EAsyncReprojectionTimewarpMode : uint8
{
	/**
	 * Render every frame (no cached-frame async timewarp).
	 */
	FullRender UMETA(DisplayName = "Full Render"),

	/**
	 * Freeze world rendering and continuously timewarp the frozen frame.
	 */
	FreezeAndWarp UMETA(DisplayName = "Freeze And Warp"),

	/**
	 * Render world at target cadence and present cached frames without warp on skipped frames.
	 */
	DecimatedNoWarp UMETA(DisplayName = "Decimated No Warp"),

	/**
	 * Render world at target cadence and timewarp cached frames on skipped frames.
	 */
	DecimatedAndWarp UMETA(DisplayName = "Decimated And Warp"),
};

/**
 * @struct FAsyncReprojectionDelta
 *
 * Delta between rendered and latest camera transforms.
 */
USTRUCT(BlueprintType)
struct FAsyncReprojectionDelta
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AsyncReprojection")
	FRotator DeltaRotationDegrees = FRotator::ZeroRotator;

	UPROPERTY(BlueprintReadOnly, Category = "AsyncReprojection")
	FVector DeltaTranslationCm = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "AsyncReprojection")
	bool bDepthAvailable = false;

	UPROPERTY(BlueprintReadOnly, Category = "AsyncReprojection")
	bool bTranslationEnabled = false;

	UPROPERTY(BlueprintReadOnly, Category = "AsyncReprojection")
	float StrengthWeight = 0.0f;
};
