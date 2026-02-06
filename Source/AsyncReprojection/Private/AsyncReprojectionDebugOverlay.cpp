// Copyright © 2023–2026 Segritude Ltd. All Rights Reserved.

#include "AsyncReprojectionDebugOverlay.h"

#include "Engine/Engine.h"
#include "ScreenPass.h"

namespace AsyncReprojectionDebugOverlay
{
	static FString BoolToOnOff(bool bValue)
	{
		return bValue ? TEXT("On") : TEXT("Off");
	}

	void AddOverlayPass(FRDGBuilder& GraphBuilder, const FSceneView& View, const FScreenPassRenderTarget& Output, const FAsyncReprojectionOverlayData& Data)
	{
		if (!Output.IsValid())
		{
			return;
		}

		AddDrawCanvasPass(GraphBuilder, RDG_EVENT_NAME("AsyncReprojection DebugOverlay"), View, Output, [&View, Data](FCanvas& Canvas)
		{
			const float X = 32.0f;
			float Y = 32.0f;
			const float Line = 14.0f;

			auto Draw = [&](const FString& Text, const FLinearColor& Color)
			{
				Canvas.DrawShadowedString(X, Y, *Text, GEngine->GetSmallFont(), Color);
				Y += Line;
			};

				Draw(FString::Printf(TEXT("AsyncReprojection: %s (%s)  Active=%s"), *Data.ModeString, *Data.WarpPointString, *BoolToOnOff(Data.bActive)), FLinearColor::White);
				Draw(FString::Printf(TEXT("FPS=%.1f  Refresh=%.1fHz  CPU Submit=%.3fms"), Data.FPS, Data.RefreshHz, Data.CpuSubmitMs), FLinearColor::White);
				Draw(FString::Printf(TEXT("DeltaRot(deg) Yaw=%.2f Pitch=%.2f Roll=%.2f"), Data.DeltaRotDegrees.Yaw, Data.DeltaRotDegrees.Pitch, Data.DeltaRotDegrees.Roll), FLinearColor::White);
				Draw(FString::Printf(TEXT("DeltaTrans=%.2fcm  Depth=%s  Translation=%s  Weight=%.2f"), Data.DeltaTransCm, *BoolToOnOff(Data.bDepthAvailable), *BoolToOnOff(Data.bTranslationEnabled), Data.Weight), FLinearColor::White);
				Draw(FString::Printf(TEXT("AsyncPresent=%s"), *BoolToOnOff(Data.bAsyncPresentEnabled)), FLinearColor::White);
			});
		}
	}
