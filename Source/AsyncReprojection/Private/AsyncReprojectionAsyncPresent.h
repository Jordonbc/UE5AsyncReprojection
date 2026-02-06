// Copyright © 2023–2026 Segritude Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * @class FAsyncReprojectionAsyncPresent
 *
 * Controls world-render decimation for Async Present (game thread).
 */
class FAsyncReprojectionAsyncPresent final
{
public:
	static FAsyncReprojectionAsyncPresent& Get();

	void Startup();
	void Shutdown();

	/**
	 * Returns true if this frame should skip world rendering (game thread decision).
	 */
	bool ShouldSkipWorldRendering() const;

	/**
	 * Signals that a skipped async-present frame could not use cached resources on the render thread.
	 * The next game-thread frame will force world rendering to re-prime the cache.
	 */
	void ReportCacheMiss_RenderThread();

private:
	FAsyncReprojectionAsyncPresent() = default;
	~FAsyncReprojectionAsyncPresent() = default;

	void OnBeginFrame_GameThread();

	void ApplyWorldRenderPreference_GameThread(bool bEnableWorldRendering);
	void RestoreWorldRenderPreference_GameThread();

private:
	mutable FCriticalSection StateLock;
	bool bStarted = false;
	bool bCachedStateValid = false;

	bool bSkipWorldRenderingThisFrame = false;
	double LastWorldRenderTimeSeconds = 0.0;

	bool bCachedDisableWorldRendering = false;
	bool bCachedShowFlagGame = true;
	bool bCachedShowFlagRendering = true;
	bool bLastLoggedSkipWorldRendering = false;
	bool bLastLoggedHasCache = false;
	bool bHasLoggedState = false;
	uint64 LastVerboseLogFrame = 0;
	TAtomic<bool> bForceWorldRenderNextFrame { false };

	FDelegateHandle BeginFrameHandle;
};
