// Copyright © 2023–2026 Segritude Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Framework/Application/IInputProcessor.h"

/**
 * @class FAsyncReprojectionInputProcessor
 *
 * Slate input preprocessor that accumulates mouse deltas for InputDrivenPose.
 */
class FAsyncReprojectionInputProcessor final : public IInputProcessor
{
public:
	virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override;
	virtual bool HandleMouseMoveEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override;
};
