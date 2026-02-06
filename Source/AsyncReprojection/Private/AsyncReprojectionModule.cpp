// Copyright © 2023–2026 Segritude Ltd. All Rights Reserved.

#include "AsyncReprojection.h"

#include "AsyncReprojectionAsyncPresent.h"
#include "AsyncReprojectionCameraTracker.h"
#include "AsyncReprojectionCVars.h"
#include "AsyncReprojectionInputProcessor.h"
#include "AsyncReprojectionSettings.h"
#include "AsyncReprojectionViewExtension.h"
#include "AsyncReprojectionWarpPass.h"

#include "Interfaces/IPluginManager.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/CoreDelegates.h"
#include "ShaderCompilerCore.h"
#include "Rendering/SlateRenderer.h"

DEFINE_LOG_CATEGORY(LogAsyncReprojection);

void FAsyncReprojectionModule::StartupModule()
{
	UE_LOG(LogAsyncReprojection, Log, TEXT("StartupModule: initializing AsyncReprojection plugin."));

	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("AsyncReprojection"));
	if (Plugin.IsValid())
	{
		const FString PluginShaderDir = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders"));
		AddShaderSourceDirectoryMapping(TEXT("/Plugin/AsyncReprojection"), PluginShaderDir);
		UE_LOG(LogAsyncReprojection, Log, TEXT("Mapped shader directory: %s"), *PluginShaderDir);
	}
	else
	{
		UE_LOG(LogAsyncReprojection, Error, TEXT("StartupModule: failed to locate AsyncReprojection plugin descriptor."));
	}

	TryInitCVarsFromSettings();
	FAsyncReprojectionCameraTracker::Get().Startup();
	FAsyncReprojectionAsyncPresent::Get().Startup();

	TryRegisterViewExtension();
	if (!bViewExtensionRegistered && !PostEngineInitHandle.IsValid())
	{
		PostEngineInitHandle = FCoreDelegates::OnPostEngineInit.AddRaw(this, &FAsyncReprojectionModule::OnPostEngineInit);
		UE_LOG(LogAsyncReprojection, Log, TEXT("Deferring view extension registration until OnPostEngineInit."));
	}

	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication& App = FSlateApplication::Get();

		InputProcessor = MakeShared<FAsyncReprojectionInputProcessor>();
		App.RegisterInputPreProcessor(InputProcessor, 0);
		UE_LOG(LogAsyncReprojection, Log, TEXT("Registered AsyncReprojection input processor."));

		FSlateRenderer* SlateRenderer = App.GetRenderer();
		if (SlateRenderer != nullptr)
		{
			BackBufferPassHandle = SlateRenderer->OnAddBackBufferReadyToPresentPass().AddRaw(this, &FAsyncReprojectionModule::OnAddBackBufferReadyToPresentPass_RenderThread);
			UE_LOG(LogAsyncReprojection, Log, TEXT("Attached back-buffer warp delegate."));
		}
		else
		{
			UE_LOG(LogAsyncReprojection, Warning, TEXT("Slate renderer is null. Back-buffer async reprojection passes are disabled."));
		}
	}
	else
	{
		UE_LOG(LogAsyncReprojection, Warning, TEXT("Slate application not initialized during startup. Input/back-buffer hooks are unavailable."));
	}
}

void FAsyncReprojectionModule::ShutdownModule()
{
	UE_LOG(LogAsyncReprojection, Log, TEXT("ShutdownModule: tearing down AsyncReprojection plugin."));

	if (PostEngineInitHandle.IsValid())
	{
		FCoreDelegates::OnPostEngineInit.Remove(PostEngineInitHandle);
		PostEngineInitHandle.Reset();
		UE_LOG(LogAsyncReprojection, Verbose, TEXT("Removed deferred OnPostEngineInit handle."));
	}

	if (FSlateApplication::IsInitialized())
	{
		if (InputProcessor.IsValid())
		{
			FSlateApplication::Get().UnregisterInputPreProcessor(InputProcessor);
			InputProcessor.Reset();
		}

		FSlateRenderer* SlateRenderer = FSlateApplication::Get().GetRenderer();
		if (SlateRenderer != nullptr && BackBufferPassHandle.IsValid())
		{
			SlateRenderer->OnAddBackBufferReadyToPresentPass().Remove(BackBufferPassHandle);
			UE_LOG(LogAsyncReprojection, Verbose, TEXT("Removed back-buffer warp delegate."));
		}
	}

	BackBufferPassHandle = FDelegateHandle();
	ViewExtension.Reset();
	bViewExtensionRegistered = false;
	bCVarsInitialized = false;

	FAsyncReprojectionAsyncPresent::Get().Shutdown();
	FAsyncReprojectionCameraTracker::Get().Shutdown();
}

void FAsyncReprojectionModule::OnAddBackBufferReadyToPresentPass_RenderThread(FRDGBuilder& GraphBuilder, SWindow& SlateWindow, FRDGTexture* BackBuffer)
{
	UE_LOG(LogAsyncReprojection, VeryVerbose, TEXT("BackBufferReadyToPresent pass hook invoked (BackBuffer=%p)."), BackBuffer);

	FAsyncReprojectionCachedPresentWarp::AddBackBufferPassIfEnabled(GraphBuilder, SlateWindow, BackBuffer);
	FAsyncReprojectionBackBufferWarp::AddPassIfEnabled(GraphBuilder, SlateWindow, BackBuffer);
}

void FAsyncReprojectionModule::OnPostEngineInit()
{
	if (PostEngineInitHandle.IsValid())
	{
		FCoreDelegates::OnPostEngineInit.Remove(PostEngineInitHandle);
		PostEngineInitHandle.Reset();
	}

	TryInitCVarsFromSettings();
	TryRegisterViewExtension();
	if (!bViewExtensionRegistered)
	{
		UE_LOG(LogAsyncReprojection, Warning, TEXT("OnPostEngineInit fired but AsyncReprojection view extension registration still failed."));
	}
}

void FAsyncReprojectionModule::TryRegisterViewExtension()
{
	if (bViewExtensionRegistered)
	{
		return;
	}

	if (GEngine == nullptr || GEngine->ViewExtensions == nullptr)
	{
		UE_LOG(LogAsyncReprojection, Verbose, TEXT("TryRegisterViewExtension postponed: GEngine or ViewExtensions is unavailable."));
		return;
	}

	ViewExtension = FSceneViewExtensions::NewExtension<FAsyncReprojectionViewExtension>(&FAsyncReprojectionCameraTracker::Get());
	bViewExtensionRegistered = ViewExtension.IsValid();

	if (bViewExtensionRegistered)
	{
		UE_LOG(LogAsyncReprojection, Log, TEXT("AsyncReprojection view extension registered successfully."));
	}
	else
	{
		UE_LOG(LogAsyncReprojection, Error, TEXT("Failed to register AsyncReprojection view extension."));
	}
}

void FAsyncReprojectionModule::TryInitCVarsFromSettings()
{
	if (bCVarsInitialized)
	{
		return;
	}

	if (GEngine == nullptr)
	{
		UE_LOG(LogAsyncReprojection, Verbose, TEXT("TryInitCVarsFromSettings postponed: GEngine is unavailable."));
		return;
	}

	FAsyncReprojectionCVars::Init();
	bCVarsInitialized = true;
	UE_LOG(LogAsyncReprojection, Log, TEXT("Initialized AsyncReprojection CVars from project settings."));
}

IMPLEMENT_MODULE(FAsyncReprojectionModule, AsyncReprojection)
