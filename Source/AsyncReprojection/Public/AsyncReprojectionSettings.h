// Copyright © 2023–2026 Segritude Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AsyncReprojectionTypes.h"
#include "Engine/DeveloperSettings.h"

#include "AsyncReprojectionSettings.generated.h"

/**
 * @class UAsyncReprojectionSettings
 *
 * Project settings for the AsyncReprojection plugin.
 */
UCLASS(Config = Engine, DefaultConfig, meta = (DisplayName = "Async Reprojection"))
class ASYNCREPROJECTION_API UAsyncReprojectionSettings final : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(Config, EditAnywhere, Category = "AsyncReprojection")
	EAsyncReprojectionMode Mode = EAsyncReprojectionMode::Auto;

	/**
	 * Enables Async Present (reproject cached scene image on intermediate frames while world rendering is decimated).
	 */
	UPROPERTY(Config, EditAnywhere, Category = "AsyncReprojection|AsyncPresent")
	bool bAsyncPresent = false;

	/**
	 * Target world render FPS when Async Present is enabled. The engine can still present more frequently if able.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "AsyncReprojection|AsyncPresent", meta = (ClampMin = "1.0", Units = "Hz"))
	float AsyncPresentTargetWorldRenderFPS = 30.0f;

	/**
	 * If enabled, freezes world rendering after at least one cached frame is available.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "AsyncReprojection|AsyncPresent")
	bool bAsyncPresentFreezeWorldRendering = false;

	/**
	 * Maximum cache age (ms) before the cached-frame warp fades out to 0 strength.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "AsyncReprojection|AsyncPresent", meta = (ClampMin = "0.0", Units = "ms"))
	int32 AsyncPresentMaxCacheAgeMs = 250;

	/**
	 * When enabled, allows cached-frame warp to occur before Slate draws UI (UI remains stable).
	 */
	UPROPERTY(Config, EditAnywhere, Category = "AsyncReprojection|AsyncPresent")
	bool bAsyncPresentAllowHUDStable = true;

	/**
	 * When enabled, translation warp is permitted on cached frames using cached depth (may artifact as depth becomes stale).
	 */
	UPROPERTY(Config, EditAnywhere, Category = "AsyncReprojection|AsyncPresent")
	bool bAsyncPresentReprojectMovement = true;

	UPROPERTY(Config, EditAnywhere, Category = "AsyncReprojection|Features")
	bool bEnableRotationWarp = true;

	UPROPERTY(Config, EditAnywhere, Category = "AsyncReprojection|Features")
	bool bEnableTranslationWarp = true;

	UPROPERTY(Config, EditAnywhere, Category = "AsyncReprojection|Features")
	bool bRequireDepthForTranslation = true;

	UPROPERTY(Config, EditAnywhere, Category = "AsyncReprojection|Clamps", meta = (ClampMin = "0.0", Units = "deg"))
	float MaxYawDegreesPerFrame = 2.0f;

	UPROPERTY(Config, EditAnywhere, Category = "AsyncReprojection|Clamps", meta = (ClampMin = "0.0", Units = "deg"))
	float MaxPitchDegreesPerFrame = 2.0f;

	UPROPERTY(Config, EditAnywhere, Category = "AsyncReprojection|Clamps", meta = (ClampMin = "0.0", Units = "deg"))
	float MaxRollDegreesPerFrame = 3.0f;

	UPROPERTY(Config, EditAnywhere, Category = "AsyncReprojection|Clamps", meta = (ClampMin = "0.0", Units = "cm"))
	float MaxTranslationCmPerFrame = 5.0f;

	UPROPERTY(Config, EditAnywhere, Category = "AsyncReprojection|Auto", meta = (ClampMin = "0.0", Units = "Hz"))
	float AutoMinRefreshDeltaHz = 10.0f;

	UPROPERTY(Config, EditAnywhere, Category = "AsyncReprojection|Auto", meta = (ClampMin = "0.0", Units = "ms"))
	int32 AutoMinStableFPSWindowMs = 500;

	UPROPERTY(Config, EditAnywhere, Category = "AsyncReprojection|Auto", meta = (ClampMin = "0.0"))
	float AutoMaxFPSStdDev = 1.5f;

	UPROPERTY(Config, EditAnywhere, Category = "AsyncReprojection|Auto", meta = (ClampMin = "0.0", Units = "deg"))
	float AutoMaxWarpDegrees = 1.5f;

	UPROPERTY(Config, EditAnywhere, Category = "AsyncReprojection|Auto", meta = (ClampMin = "0.0", Units = "cm"))
	float AutoMaxTranslationCm = 3.0f;

	UPROPERTY(Config, EditAnywhere, Category = "AsyncReprojection|UI")
	bool bWarpAfterUI = false;

	UPROPERTY(Config, EditAnywhere, Category = "AsyncReprojection|Pipeline")
	EAsyncReprojectionWarpPoint WarpPoint = EAsyncReprojectionWarpPoint::PostRenderViewFamily;

	UPROPERTY(Config, EditAnywhere, Category = "AsyncReprojection|Debug")
	bool bDebugOverlay = false;

	UPROPERTY(Config, EditAnywhere, Category = "AsyncReprojection|Debug")
	bool bDebugFreezeWarp = false;
};
