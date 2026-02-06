// Copyright © 2023–2026 Segritude Ltd. All Rights Reserved.

#include "AsyncReprojectionInputProcessor.h"

#include "AsyncReprojectionCameraTracker.h"
#include "AsyncReprojectionCVars.h"

#include "Framework/Application/SlateApplication.h"

void FAsyncReprojectionInputProcessor::Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor)
{
	(void)DeltaTime;
	(void)SlateApp;
	(void)Cursor;
}

bool FAsyncReprojectionInputProcessor::HandleMouseMoveEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
	(void)SlateApp;

	const FAsyncReprojectionCVarState CVarState = FAsyncReprojectionCVars::Get();
	if (!CVarState.bInputDrivenPose)
	{
		return false;
	}

	const int32 PlayerIndex = int32(MouseEvent.GetUserIndex());
	const FVector2D Delta = MouseEvent.GetCursorDelta();
	FAsyncReprojectionCameraTracker::Get().AddMouseDelta_GameThread(PlayerIndex, float(Delta.X), float(Delta.Y));

	return false;
}
