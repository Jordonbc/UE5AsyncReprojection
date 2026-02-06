// Copyright © 2023–2026 Segritude Ltd. All Rights Reserved.

#include "AsyncReprojectionAsyncPresent.h"

#include "AsyncReprojection.h"
#include "AsyncReprojectionCVars.h"
#include "AsyncReprojectionFrameCache.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Misc/CoreDelegates.h"

namespace AsyncReprojectionAsyncPresentPrivate
{
	static constexpr uint64 VerboseLogFrameInterval = 120;
}

FAsyncReprojectionAsyncPresent& FAsyncReprojectionAsyncPresent::Get()
{
	static FAsyncReprojectionAsyncPresent Instance;
	return Instance;
}

void FAsyncReprojectionAsyncPresent::Startup()
{
	FScopeLock Lock(&StateLock);
	if (bStarted)
	{
		UE_LOG(LogAsyncReprojection, Verbose, TEXT("AsyncPresent startup requested but already active."));
		return;
	}

	bStarted = true;
	LastWorldRenderTimeSeconds = 0.0;
	bSkipWorldRenderingThisFrame = false;
	bHasLoggedState = false;
	LastVerboseLogFrame = 0;

	BeginFrameHandle = FCoreDelegates::OnBeginFrame.AddRaw(this, &FAsyncReprojectionAsyncPresent::OnBeginFrame_GameThread);
	UE_LOG(LogAsyncReprojection, Log, TEXT("AsyncPresent started (OnBeginFrame registered)."));
}

void FAsyncReprojectionAsyncPresent::Shutdown()
{
	{
		FScopeLock Lock(&StateLock);
		if (!bStarted)
		{
			return;
		}

		bStarted = false;
	}

	if (BeginFrameHandle.IsValid())
	{
		FCoreDelegates::OnBeginFrame.Remove(BeginFrameHandle);
		BeginFrameHandle = FDelegateHandle();
	}

	RestoreWorldRenderPreference_GameThread();
	UE_LOG(LogAsyncReprojection, Log, TEXT("AsyncPresent shut down."));
}

bool FAsyncReprojectionAsyncPresent::ShouldSkipWorldRendering() const
{
	FScopeLock Lock(&StateLock);
	return bSkipWorldRenderingThisFrame;
}

void FAsyncReprojectionAsyncPresent::ReportCacheMiss_RenderThread()
{
	bForceWorldRenderNextFrame.Store(true);
}

void FAsyncReprojectionAsyncPresent::OnBeginFrame_GameThread()
{
	check(IsInGameThread());

	const FAsyncReprojectionCVarState CVarState = FAsyncReprojectionCVars::Get();
	if (!CVarState.bAsyncPresent)
	{
		{
			FScopeLock Lock(&StateLock);
			bSkipWorldRenderingThisFrame = false;
		}

		RestoreWorldRenderPreference_GameThread();
		return;
	}

	const double NowSeconds = FPlatformTime::Seconds();
	const double PeriodSeconds = 1.0 / FMath::Max(1.0f, CVarState.AsyncPresentTargetWorldRenderFPS);

	bool bEnableWorldRendering = true;
	const bool bHasCachedFrame = FAsyncReprojectionFrameCache::Get().HasCachedFrame_AnyThread(0);
	const bool bHasUsableCachedFrame = FAsyncReprojectionFrameCache::Get().HasUsableCachedFrame_AnyThread(0, NowSeconds, CVarState.AsyncPresentMaxCacheAgeMs, 1);
	const bool bForceWorldRender = bForceWorldRenderNextFrame.Exchange(false);

	if (bForceWorldRender)
	{
		bEnableWorldRendering = true;
	}
	else if (!bHasUsableCachedFrame)
	{
		bEnableWorldRendering = true;
	}
	else if (CVarState.bAsyncPresentFreezeWorldRendering)
	{
		bEnableWorldRendering = false;
	}
	else
	{
		if (LastWorldRenderTimeSeconds <= 0.0)
		{
			bEnableWorldRendering = true;
		}
		else
		{
			bEnableWorldRendering = (NowSeconds - LastWorldRenderTimeSeconds) >= PeriodSeconds;
		}
	}

	if (bEnableWorldRendering)
	{
		LastWorldRenderTimeSeconds = NowSeconds;
	}

	ApplyWorldRenderPreference_GameThread(bEnableWorldRendering);

	{
		FScopeLock Lock(&StateLock);
		bSkipWorldRenderingThisFrame = !bEnableWorldRendering;
	}

	const bool bSkipWorld = !bEnableWorldRendering;
	if (!bHasLoggedState || bLastLoggedSkipWorldRendering != bSkipWorld || bLastLoggedHasCache != bHasCachedFrame)
	{
		UE_LOG(
			LogAsyncReprojection,
			Log,
			TEXT("AsyncPresent state changed: SkipWorld=%d HasCache=%d HasUsableCache=%d ForceWorldRender=%d Freeze=%d TargetFPS=%.2f"),
			bSkipWorld ? 1 : 0,
			bHasCachedFrame ? 1 : 0,
			bHasUsableCachedFrame ? 1 : 0,
			bForceWorldRender ? 1 : 0,
			CVarState.bAsyncPresentFreezeWorldRendering ? 1 : 0,
			CVarState.AsyncPresentTargetWorldRenderFPS);

		bLastLoggedSkipWorldRendering = bSkipWorld;
		bLastLoggedHasCache = bHasCachedFrame;
		bHasLoggedState = true;
	}
	else if ((GFrameCounter - LastVerboseLogFrame) >= AsyncReprojectionAsyncPresentPrivate::VerboseLogFrameInterval)
	{
		UE_LOG(
			LogAsyncReprojection,
			Verbose,
			TEXT("AsyncPresent tick: SkipWorld=%d HasCache=%d PeriodMs=%.2f"),
			bSkipWorld ? 1 : 0,
			bHasCachedFrame ? 1 : 0,
			PeriodSeconds * 1000.0);
		LastVerboseLogFrame = GFrameCounter;
	}
}

void FAsyncReprojectionAsyncPresent::ApplyWorldRenderPreference_GameThread(bool bEnableWorldRendering)
{
	if (GEngine == nullptr || GEngine->GameViewport == nullptr)
	{
		UE_LOG(LogAsyncReprojection, Warning, TEXT("ApplyWorldRenderPreference skipped: GameViewport is unavailable."));
		return;
	}

	UGameViewportClient* GameViewport = GEngine->GameViewport;
	FEngineShowFlags& ShowFlags = GameViewport->EngineShowFlags;

	if (!bCachedStateValid)
	{
		bCachedStateValid = true;
		bCachedDisableWorldRendering = GameViewport->bDisableWorldRendering;
		bCachedShowFlagGame = ShowFlags.Game;
		bCachedShowFlagRendering = ShowFlags.Rendering;
	}

	if (bEnableWorldRendering)
	{
		GameViewport->bDisableWorldRendering = false;
		ShowFlags.SetRendering(true);
		ShowFlags.SetGame(true);
	}
	else
	{
		GameViewport->bDisableWorldRendering = true;
		ShowFlags.SetRendering(true);
		ShowFlags.SetGame(true);
	}

	UE_LOG(
		LogAsyncReprojection,
		VeryVerbose,
		TEXT("ApplyWorldRenderPreference: EnableWorldRendering=%d DisableWorldRenderingFlag=%d"),
		bEnableWorldRendering ? 1 : 0,
		GameViewport->bDisableWorldRendering ? 1 : 0);
}

void FAsyncReprojectionAsyncPresent::RestoreWorldRenderPreference_GameThread()
{
	check(IsInGameThread());

	if (!bCachedStateValid)
	{
		return;
	}

	if (GEngine == nullptr || GEngine->GameViewport == nullptr)
	{
		UE_LOG(LogAsyncReprojection, Warning, TEXT("RestoreWorldRenderPreference skipped: GameViewport is unavailable."));
		bCachedStateValid = false;
		return;
	}

	UGameViewportClient* GameViewport = GEngine->GameViewport;
	FEngineShowFlags& ShowFlags = GameViewport->EngineShowFlags;

	GameViewport->bDisableWorldRendering = bCachedDisableWorldRendering;
	ShowFlags.SetGame(bCachedShowFlagGame);
	ShowFlags.SetRendering(bCachedShowFlagRendering);

	bCachedStateValid = false;
	UE_LOG(LogAsyncReprojection, Verbose, TEXT("Restored world render preference to cached viewport values."));
}
