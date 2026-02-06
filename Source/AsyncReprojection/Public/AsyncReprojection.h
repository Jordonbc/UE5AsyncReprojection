// Copyright © 2023–2026 Segritude Ltd. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"

class FRDGBuilder;
class SWindow;
class FAsyncReprojectionViewExtension;
class FAsyncReprojectionInputProcessor;
class FRDGTexture;

DECLARE_LOG_CATEGORY_EXTERN(LogAsyncReprojection, Log, All);

/**
 * @class FAsyncReprojectionModule
 *
 * Plugin module wiring (view extension registration and optional WarpAfterUI hook).
 */
class FAsyncReprojectionModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void OnAddBackBufferReadyToPresentPass_RenderThread(FRDGBuilder& GraphBuilder, SWindow& SlateWindow, FRDGTexture* BackBuffer);
	void OnPostEngineInit();
	void TryRegisterViewExtension();
	void TryInitCVarsFromSettings();

private:
	TSharedPtr<FAsyncReprojectionViewExtension, ESPMode::ThreadSafe> ViewExtension;
	TSharedPtr<FAsyncReprojectionInputProcessor> InputProcessor;
	FDelegateHandle BackBufferPassHandle;
	FDelegateHandle PostEngineInitHandle;
	bool bViewExtensionRegistered = false;
	bool bCVarsInitialized = false;
};
