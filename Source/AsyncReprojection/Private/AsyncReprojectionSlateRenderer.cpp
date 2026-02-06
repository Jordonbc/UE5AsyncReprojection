// Copyright © 2023–2026 Segritude Ltd. All Rights Reserved.

#include "AsyncReprojectionSlateRenderer.h"

#include "AsyncReprojectionAsyncPresent.h"
#include "AsyncReprojectionCVars.h"
#include "AsyncReprojectionWarpPass.h"

#include "DynamicRHI.h"
#include "Rendering/SlateDrawBuffer.h"
#include "RenderingThread.h"

FAsyncReprojectionSlateRenderer::FAsyncReprojectionSlateRenderer(TSharedRef<FSlateRenderer> InUnderlyingRenderer)
	: FSlateRenderer(InUnderlyingRenderer->GetFontServices())
	, UnderlyingRenderer(InUnderlyingRenderer)
{
	SlateWindowRendered = MoveTemp(InUnderlyingRenderer->OnSlateWindowRendered());
	OnSlateWindowDestroyedDelegate = MoveTemp(InUnderlyingRenderer->OnSlateWindowDestroyed());
	PreResizeBackBufferDelegate = MoveTemp(InUnderlyingRenderer->OnPreResizeWindowBackBuffer());
	PostResizeBackBufferDelegate = MoveTemp(InUnderlyingRenderer->OnPostResizeWindowBackBuffer());
	OnBackBufferReadyToPresentDelegate = MoveTemp(InUnderlyingRenderer->OnBackBufferReadyToPresent());
	OnAddBackBufferReadyToPresentPassDelegate = MoveTemp(InUnderlyingRenderer->OnAddBackBufferReadyToPresentPass());

	InUnderlyingRenderer->OnSlateWindowRendered().Clear();
	InUnderlyingRenderer->OnSlateWindowDestroyed().Clear();
	InUnderlyingRenderer->OnPreResizeWindowBackBuffer().Clear();
	InUnderlyingRenderer->OnPostResizeWindowBackBuffer().Clear();
	InUnderlyingRenderer->OnBackBufferReadyToPresent().Clear();
	InUnderlyingRenderer->OnAddBackBufferReadyToPresentPass().Clear();

	OnSlateWindowRenderedHandle = InUnderlyingRenderer->OnSlateWindowRendered().AddRaw(this, &FAsyncReprojectionSlateRenderer::OnSlateWindowRenderedThunk);
	OnSlateWindowDestroyedHandle = InUnderlyingRenderer->OnSlateWindowDestroyed().AddRaw(this, &FAsyncReprojectionSlateRenderer::OnSlateWindowDestroyedThunk);
	OnPreResizeWindowBackBufferHandle = InUnderlyingRenderer->OnPreResizeWindowBackBuffer().AddRaw(this, &FAsyncReprojectionSlateRenderer::OnPreResizeWindowBackBufferThunk);
	OnPostResizeWindowBackBufferHandle = InUnderlyingRenderer->OnPostResizeWindowBackBuffer().AddRaw(this, &FAsyncReprojectionSlateRenderer::OnPostResizeWindowBackBufferThunk);
	OnBackBufferReadyToPresentHandle = InUnderlyingRenderer->OnBackBufferReadyToPresent().AddRaw(this, &FAsyncReprojectionSlateRenderer::OnBackBufferReadyToPresentThunk);
	OnAddBackBufferReadyToPresentPassHandle = InUnderlyingRenderer->OnAddBackBufferReadyToPresentPass().AddRaw(this, &FAsyncReprojectionSlateRenderer::OnAddBackBufferReadyToPresentPassThunk);
}

FAsyncReprojectionSlateRenderer::~FAsyncReprojectionSlateRenderer()
{
	if (OnSlateWindowRenderedHandle.IsValid())
	{
		UnderlyingRenderer->OnSlateWindowRendered().Remove(OnSlateWindowRenderedHandle);
	}
	if (OnSlateWindowDestroyedHandle.IsValid())
	{
		UnderlyingRenderer->OnSlateWindowDestroyed().Remove(OnSlateWindowDestroyedHandle);
	}
	if (OnPreResizeWindowBackBufferHandle.IsValid())
	{
		UnderlyingRenderer->OnPreResizeWindowBackBuffer().Remove(OnPreResizeWindowBackBufferHandle);
	}
	if (OnPostResizeWindowBackBufferHandle.IsValid())
	{
		UnderlyingRenderer->OnPostResizeWindowBackBuffer().Remove(OnPostResizeWindowBackBufferHandle);
	}
	if (OnBackBufferReadyToPresentHandle.IsValid())
	{
		UnderlyingRenderer->OnBackBufferReadyToPresent().Remove(OnBackBufferReadyToPresentHandle);
	}
	if (OnAddBackBufferReadyToPresentPassHandle.IsValid())
	{
		UnderlyingRenderer->OnAddBackBufferReadyToPresentPass().Remove(OnAddBackBufferReadyToPresentPassHandle);
	}
}

void FAsyncReprojectionSlateRenderer::RestoreUnderlyingDelegates()
{
	if (OnSlateWindowRenderedHandle.IsValid())
	{
		UnderlyingRenderer->OnSlateWindowRendered().Remove(OnSlateWindowRenderedHandle);
		OnSlateWindowRenderedHandle.Reset();
	}
	if (OnSlateWindowDestroyedHandle.IsValid())
	{
		UnderlyingRenderer->OnSlateWindowDestroyed().Remove(OnSlateWindowDestroyedHandle);
		OnSlateWindowDestroyedHandle.Reset();
	}
	if (OnPreResizeWindowBackBufferHandle.IsValid())
	{
		UnderlyingRenderer->OnPreResizeWindowBackBuffer().Remove(OnPreResizeWindowBackBufferHandle);
		OnPreResizeWindowBackBufferHandle.Reset();
	}
	if (OnPostResizeWindowBackBufferHandle.IsValid())
	{
		UnderlyingRenderer->OnPostResizeWindowBackBuffer().Remove(OnPostResizeWindowBackBufferHandle);
		OnPostResizeWindowBackBufferHandle.Reset();
	}
	if (OnBackBufferReadyToPresentHandle.IsValid())
	{
		UnderlyingRenderer->OnBackBufferReadyToPresent().Remove(OnBackBufferReadyToPresentHandle);
		OnBackBufferReadyToPresentHandle.Reset();
	}
	if (OnAddBackBufferReadyToPresentPassHandle.IsValid())
	{
		UnderlyingRenderer->OnAddBackBufferReadyToPresentPass().Remove(OnAddBackBufferReadyToPresentPassHandle);
		OnAddBackBufferReadyToPresentPassHandle.Reset();
	}

	UnderlyingRenderer->OnSlateWindowRendered() = MoveTemp(SlateWindowRendered);
	UnderlyingRenderer->OnSlateWindowDestroyed() = MoveTemp(OnSlateWindowDestroyedDelegate);
	UnderlyingRenderer->OnPreResizeWindowBackBuffer() = MoveTemp(PreResizeBackBufferDelegate);
	UnderlyingRenderer->OnPostResizeWindowBackBuffer() = MoveTemp(PostResizeBackBufferDelegate);
	UnderlyingRenderer->OnBackBufferReadyToPresent() = MoveTemp(OnBackBufferReadyToPresentDelegate);
	UnderlyingRenderer->OnAddBackBufferReadyToPresentPass() = MoveTemp(OnAddBackBufferReadyToPresentPassDelegate);
}

#if UE_VERSION_AT_LEAST(5, 1, 0)
FSlateDrawBuffer& FAsyncReprojectionSlateRenderer::AcquireDrawBuffer()
{
	return UnderlyingRenderer->AcquireDrawBuffer();
}

void FAsyncReprojectionSlateRenderer::ReleaseDrawBuffer(FSlateDrawBuffer& InWindowDrawBuffer)
{
	UnderlyingRenderer->ReleaseDrawBuffer(InWindowDrawBuffer);
}
#else
FSlateDrawBuffer& FAsyncReprojectionSlateRenderer::GetDrawBuffer()
{
	return UnderlyingRenderer->GetDrawBuffer();
}
#endif

bool FAsyncReprojectionSlateRenderer::Initialize()
{
	return UnderlyingRenderer->Initialize();
}

void FAsyncReprojectionSlateRenderer::Destroy()
{
	UnderlyingRenderer->Destroy();
}

void FAsyncReprojectionSlateRenderer::CreateViewport(const TSharedRef<SWindow> InWindow)
{
	UnderlyingRenderer->CreateViewport(InWindow);
}

void FAsyncReprojectionSlateRenderer::RequestResize(const TSharedPtr<SWindow>& InWindow, uint32 NewSizeX, uint32 NewSizeY)
{
	UnderlyingRenderer->RequestResize(InWindow, NewSizeX, NewSizeY);
}

void FAsyncReprojectionSlateRenderer::UpdateFullscreenState(const TSharedRef<SWindow> InWindow, uint32 OverrideResX, uint32 OverrideResY)
{
	UnderlyingRenderer->UpdateFullscreenState(InWindow, OverrideResX, OverrideResY);
}

void FAsyncReprojectionSlateRenderer::SetSystemResolution(uint32 Width, uint32 Height)
{
	UnderlyingRenderer->SetSystemResolution(Width, Height);
}

void FAsyncReprojectionSlateRenderer::RestoreSystemResolution(const TSharedRef<SWindow> InWindow)
{
	UnderlyingRenderer->RestoreSystemResolution(InWindow);
}

void FAsyncReprojectionSlateRenderer::DrawWindows(FSlateDrawBuffer& InWindowDrawBuffer)
{
	const FAsyncReprojectionCVarState CVarState = FAsyncReprojectionCVars::Get();
	const bool bDoAsyncPresent = CVarState.bAsyncPresent && CVarState.bAsyncPresentAllowHUDStable && FAsyncReprojectionAsyncPresent::Get().ShouldSkipWorldRendering();

	if (bDoAsyncPresent)
	{
		TArray<FRHIViewport*, TInlineAllocator<4>> ViewportsToPrep;
		for (const TSharedRef<FSlateWindowElementList>& ElementList : InWindowDrawBuffer.GetWindowElementLists())
		{
			SWindow* Window = ElementList->GetRenderWindow();
			if (Window == nullptr)
			{
				continue;
			}

			void* ViewportResource = UnderlyingRenderer->GetViewportResource(*Window);
			if (ViewportResource == nullptr)
			{
				continue;
			}

			FViewportRHIRef* ViewportRefPtr = static_cast<FViewportRHIRef*>(ViewportResource);
			if (ViewportRefPtr == nullptr || !ViewportRefPtr->IsValid())
			{
				continue;
			}

			ViewportsToPrep.Add(ViewportRefPtr->GetReference());
		}

		if (!ViewportsToPrep.IsEmpty())
		{
			ENQUEUE_RENDER_COMMAND(AsyncReprojectionPreSlateCachedWarp)(
				[ViewportsToPrep](FRHICommandListImmediate& RHICmdList)
				{
					for (FRHIViewport* ViewportRHI : ViewportsToPrep)
					{
						FAsyncReprojectionCachedPresentWarp::AddPreSlatePassIfEnabled(RHICmdList, ViewportRHI);
					}
				});
		}
	}

	UnderlyingRenderer->DrawWindows(InWindowDrawBuffer);
}

void FAsyncReprojectionSlateRenderer::SetColorVisionDeficiencyType(EColorVisionDeficiency Type, int32 Severity, bool bCorrectDeficiency, bool bShowCorrectionWithDeficiency)
{
	UnderlyingRenderer->SetColorVisionDeficiencyType(Type, Severity, bCorrectDeficiency, bShowCorrectionWithDeficiency);
}

FIntPoint FAsyncReprojectionSlateRenderer::GenerateDynamicImageResource(const FName InTextureName)
{
	return UnderlyingRenderer->GenerateDynamicImageResource(InTextureName);
}

bool FAsyncReprojectionSlateRenderer::GenerateDynamicImageResource(FName ResourceName, uint32 Width, uint32 Height, const TArray<uint8>& Bytes)
{
	return UnderlyingRenderer->GenerateDynamicImageResource(ResourceName, Width, Height, Bytes);
}

bool FAsyncReprojectionSlateRenderer::GenerateDynamicImageResource(FName ResourceName, FSlateTextureDataRef TextureData)
{
	return UnderlyingRenderer->GenerateDynamicImageResource(ResourceName, TextureData);
}

#if UE_VERSION_AT_LEAST(5, 1, 0)
FSlateResourceHandle FAsyncReprojectionSlateRenderer::GetResourceHandle(const FSlateBrush& Brush, FVector2f LocalSize, float DrawScale)
{
	return UnderlyingRenderer->GetResourceHandle(Brush, LocalSize, DrawScale);
}
#elif UE_VERSION_AT_LEAST(5, 0, 0)
FSlateResourceHandle FAsyncReprojectionSlateRenderer::GetResourceHandle(const FSlateBrush& Brush, FVector2D LocalSize, float DrawScale)
{
	return UnderlyingRenderer->GetResourceHandle(Brush, LocalSize, DrawScale);
}
#endif

FSlateResourceHandle FAsyncReprojectionSlateRenderer::GetResourceHandle(const FSlateBrush& Brush)
{
	return UnderlyingRenderer->GetResourceHandle(Brush);
}

bool FAsyncReprojectionSlateRenderer::CanRenderResource(UObject& InResourceObject) const
{
	return UnderlyingRenderer->CanRenderResource(InResourceObject);
}

void FAsyncReprojectionSlateRenderer::RemoveDynamicBrushResource(TSharedPtr<FSlateDynamicImageBrush> BrushToRemove)
{
	UnderlyingRenderer->RemoveDynamicBrushResource(BrushToRemove);
}

void FAsyncReprojectionSlateRenderer::ReleaseDynamicResource(const FSlateBrush& InBrush)
{
	UnderlyingRenderer->ReleaseDynamicResource(InBrush);
}

void FAsyncReprojectionSlateRenderer::OnWindowDestroyed(const TSharedRef<SWindow>& InWindow)
{
	UnderlyingRenderer->OnWindowDestroyed(InWindow);
}

void FAsyncReprojectionSlateRenderer::OnWindowFinishReshaped(const TSharedPtr<SWindow>& InWindow)
{
	UnderlyingRenderer->OnWindowFinishReshaped(InWindow);
}

void* FAsyncReprojectionSlateRenderer::GetViewportResource(const SWindow& Window)
{
	return UnderlyingRenderer->GetViewportResource(Window);
}

void FAsyncReprojectionSlateRenderer::FlushCommands() const
{
	UnderlyingRenderer->FlushCommands();
}

void FAsyncReprojectionSlateRenderer::Sync() const
{
	UnderlyingRenderer->Sync();
}

void FAsyncReprojectionSlateRenderer::BeginFrame() const
{
	UnderlyingRenderer->BeginFrame();
}

void FAsyncReprojectionSlateRenderer::EndFrame() const
{
	UnderlyingRenderer->EndFrame();
}

void FAsyncReprojectionSlateRenderer::ReloadTextureResources()
{
	UnderlyingRenderer->ReloadTextureResources();
}

void FAsyncReprojectionSlateRenderer::LoadStyleResources(const ISlateStyle& Style)
{
	UnderlyingRenderer->LoadStyleResources(Style);
}

bool FAsyncReprojectionSlateRenderer::AreShadersInitialized() const
{
	return UnderlyingRenderer->AreShadersInitialized();
}

void FAsyncReprojectionSlateRenderer::InvalidateAllViewports()
{
	UnderlyingRenderer->InvalidateAllViewports();
}

void FAsyncReprojectionSlateRenderer::ReleaseAccessedResources(bool bImmediatelyFlush)
{
	UnderlyingRenderer->ReleaseAccessedResources(bImmediatelyFlush);
}

void FAsyncReprojectionSlateRenderer::PrepareToTakeScreenshot(const FIntRect& Rect, TArray<FColor>* OutColorData, SWindow* InScreenshotWindow)
{
	UnderlyingRenderer->PrepareToTakeScreenshot(Rect, OutColorData, InScreenshotWindow);
}

void FAsyncReprojectionSlateRenderer::SetWindowRenderTarget(const SWindow& Window, IViewportRenderTargetProvider* Provider)
{
	UnderlyingRenderer->SetWindowRenderTarget(Window, Provider);
}

FSlateUpdatableTexture* FAsyncReprojectionSlateRenderer::CreateUpdatableTexture(uint32 Width, uint32 Height)
{
	return UnderlyingRenderer->CreateUpdatableTexture(Width, Height);
}

FSlateUpdatableTexture* FAsyncReprojectionSlateRenderer::CreateSharedHandleTexture(void* SharedHandle)
{
	return UnderlyingRenderer->CreateSharedHandleTexture(SharedHandle);
}

void FAsyncReprojectionSlateRenderer::ReleaseUpdatableTexture(FSlateUpdatableTexture* Texture)
{
	UnderlyingRenderer->ReleaseUpdatableTexture(Texture);
}

ISlateAtlasProvider* FAsyncReprojectionSlateRenderer::GetTextureAtlasProvider()
{
	return UnderlyingRenderer->GetTextureAtlasProvider();
}

ISlateAtlasProvider* FAsyncReprojectionSlateRenderer::GetFontAtlasProvider()
{
	return UnderlyingRenderer->GetFontAtlasProvider();
}

void FAsyncReprojectionSlateRenderer::CopyWindowsToVirtualScreenBuffer(const TArray<FString>& KeypressBuffer)
{
	UnderlyingRenderer->CopyWindowsToVirtualScreenBuffer(KeypressBuffer);
}

void FAsyncReprojectionSlateRenderer::MapVirtualScreenBuffer(FMappedTextureBuffer* OutImageData)
{
	UnderlyingRenderer->MapVirtualScreenBuffer(OutImageData);
}

void FAsyncReprojectionSlateRenderer::UnmapVirtualScreenBuffer()
{
	UnderlyingRenderer->UnmapVirtualScreenBuffer();
}

FCriticalSection* FAsyncReprojectionSlateRenderer::GetResourceCriticalSection()
{
	return UnderlyingRenderer->GetResourceCriticalSection();
}

int32 FAsyncReprojectionSlateRenderer::RegisterCurrentScene(FSceneInterface* Scene)
{
	return UnderlyingRenderer->RegisterCurrentScene(Scene);
}

int32 FAsyncReprojectionSlateRenderer::GetCurrentSceneIndex() const
{
	return UnderlyingRenderer->GetCurrentSceneIndex();
}

void FAsyncReprojectionSlateRenderer::SetCurrentSceneIndex(int32 Index)
{
	UnderlyingRenderer->SetCurrentSceneIndex(Index);
}

void FAsyncReprojectionSlateRenderer::ClearScenes()
{
	UnderlyingRenderer->ClearScenes();
}

void FAsyncReprojectionSlateRenderer::DestroyCachedFastPathRenderingData(FSlateCachedFastPathRenderingData* VertexData)
{
	UnderlyingRenderer->DestroyCachedFastPathRenderingData(VertexData);
}

void FAsyncReprojectionSlateRenderer::DestroyCachedFastPathElementData(FSlateCachedElementData* ElementData)
{
	UnderlyingRenderer->DestroyCachedFastPathElementData(ElementData);
}

bool FAsyncReprojectionSlateRenderer::HasLostDevice() const
{
	return UnderlyingRenderer->HasLostDevice();
}

void FAsyncReprojectionSlateRenderer::AddWidgetRendererUpdate(const FRenderThreadUpdateContext& Context, bool bDeferredRenderTargetUpdate)
{
	UnderlyingRenderer->AddWidgetRendererUpdate(Context, bDeferredRenderTargetUpdate);
}

EPixelFormat FAsyncReprojectionSlateRenderer::GetSlateRecommendedColorFormat()
{
	return UnderlyingRenderer->GetSlateRecommendedColorFormat();
}
