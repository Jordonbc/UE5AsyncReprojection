// Copyright © 2023–2026 Segritude Ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Misc/EngineVersionComparison.h"
#include "Rendering/SlateRenderer.h"

#ifndef UE_VERSION_AT_LEAST
#define UE_VERSION_AT_LEAST(MajorVersion, MinorVersion, PatchVersion) UE_VERSION_NEWER_THAN_OR_EQUAL(MajorVersion, MinorVersion, PatchVersion)
#endif

/**
 * @class FAsyncReprojectionSlateRenderer
 *
 * Slate renderer wrapper that can inject a cached-frame reprojection pass before Slate UI is drawn.
 */
class FAsyncReprojectionSlateRenderer final : public FSlateRenderer
{
public:
	explicit FAsyncReprojectionSlateRenderer(TSharedRef<FSlateRenderer> InUnderlyingRenderer);
	virtual ~FAsyncReprojectionSlateRenderer();

	TSharedRef<FSlateRenderer> GetUnderlyingRenderer() const
	{
		return UnderlyingRenderer;
	}

	void RestoreUnderlyingDelegates();

#if UE_VERSION_AT_LEAST(5, 1, 0)
	virtual FSlateDrawBuffer& AcquireDrawBuffer() override;
	virtual void ReleaseDrawBuffer(FSlateDrawBuffer& InWindowDrawBuffer) override;
#else
	virtual FSlateDrawBuffer& GetDrawBuffer() override;
#endif

	virtual bool Initialize() override;
	virtual void Destroy() override;
	virtual void CreateViewport(const TSharedRef<SWindow> InWindow) override;
	virtual void RequestResize(const TSharedPtr<SWindow>& InWindow, uint32 NewSizeX, uint32 NewSizeY) override;
	virtual void UpdateFullscreenState(const TSharedRef<SWindow> InWindow, uint32 OverrideResX, uint32 OverrideResY) override;
	virtual void SetSystemResolution(uint32 Width, uint32 Height) override;
	virtual void RestoreSystemResolution(const TSharedRef<SWindow> InWindow) override;

	virtual void DrawWindows(FSlateDrawBuffer& InWindowDrawBuffer) override;

	virtual void SetColorVisionDeficiencyType(EColorVisionDeficiency Type, int32 Severity, bool bCorrectDeficiency, bool bShowCorrectionWithDeficiency) override;
	virtual FIntPoint GenerateDynamicImageResource(const FName InTextureName) override;
	virtual bool GenerateDynamicImageResource(FName ResourceName, uint32 Width, uint32 Height, const TArray<uint8>& Bytes) override;
		virtual bool GenerateDynamicImageResource(FName ResourceName, FSlateTextureDataRef TextureData) override;

#if UE_VERSION_AT_LEAST(5, 1, 0)
		virtual FSlateResourceHandle GetResourceHandle(const FSlateBrush& Brush, FVector2f LocalSize, float DrawScale) override;
#elif UE_VERSION_AT_LEAST(5, 0, 0)
		virtual FSlateResourceHandle GetResourceHandle(const FSlateBrush& Brush, FVector2D LocalSize, float DrawScale) override;
#endif

		virtual FSlateResourceHandle GetResourceHandle(const FSlateBrush& Brush) override;
		virtual bool CanRenderResource(UObject& InResourceObject) const override;
		virtual void RemoveDynamicBrushResource(TSharedPtr<FSlateDynamicImageBrush> BrushToRemove) override;
		virtual void ReleaseDynamicResource(const FSlateBrush& InBrush) override;

	virtual void OnWindowDestroyed(const TSharedRef<SWindow>& InWindow) override;
	virtual void OnWindowFinishReshaped(const TSharedPtr<SWindow>& InWindow) override;
	virtual void* GetViewportResource(const SWindow& Window) override;
	virtual void FlushCommands() const override;
	virtual void Sync() const override;
	virtual void BeginFrame() const override;
	virtual void EndFrame() const override;
	virtual void ReloadTextureResources() override;
	virtual void LoadStyleResources(const ISlateStyle& Style) override;
	virtual bool AreShadersInitialized() const override;
	virtual void InvalidateAllViewports() override;
	virtual void ReleaseAccessedResources(bool bImmediatelyFlush) override;
	virtual void PrepareToTakeScreenshot(const FIntRect& Rect, TArray<FColor>* OutColorData, SWindow* InScreenshotWindow) override;
	virtual void SetWindowRenderTarget(const SWindow& Window, class IViewportRenderTargetProvider* Provider) override;
	virtual FSlateUpdatableTexture* CreateUpdatableTexture(uint32 Width, uint32 Height) override;
	virtual FSlateUpdatableTexture* CreateSharedHandleTexture(void* SharedHandle) override;
	virtual void ReleaseUpdatableTexture(FSlateUpdatableTexture* Texture) override;
	virtual ISlateAtlasProvider* GetTextureAtlasProvider() override;
	virtual ISlateAtlasProvider* GetFontAtlasProvider() override;
	virtual void CopyWindowsToVirtualScreenBuffer(const TArray<FString>& KeypressBuffer) override;
	virtual void MapVirtualScreenBuffer(FMappedTextureBuffer* OutImageData) override;
	virtual void UnmapVirtualScreenBuffer() override;
		virtual FCriticalSection* GetResourceCriticalSection() override;

	virtual int32 RegisterCurrentScene(FSceneInterface* Scene) override;
	virtual int32 GetCurrentSceneIndex() const override;
	virtual void SetCurrentSceneIndex(int32 Index) override;
	virtual void ClearScenes() override;
	virtual void DestroyCachedFastPathRenderingData(struct FSlateCachedFastPathRenderingData* VertexData) override;
	virtual void DestroyCachedFastPathElementData(struct FSlateCachedElementData* ElementData) override;
	virtual bool HasLostDevice() const override;
	virtual void AddWidgetRendererUpdate(const struct FRenderThreadUpdateContext& Context, bool bDeferredRenderTargetUpdate) override;
	virtual EPixelFormat GetSlateRecommendedColorFormat() override;

	private:
	void OnSlateWindowRenderedThunk(SWindow& Window, void* Ptr)
	{
		SlateWindowRendered.Broadcast(Window, Ptr);
	}

	void OnSlateWindowDestroyedThunk(void* Ptr)
	{
		OnSlateWindowDestroyedDelegate.Broadcast(Ptr);
	}

	void OnPreResizeWindowBackBufferThunk(void* Ptr)
	{
		PreResizeBackBufferDelegate.Broadcast(Ptr);
	}

	void OnPostResizeWindowBackBufferThunk(void* Ptr)
	{
		PostResizeBackBufferDelegate.Broadcast(Ptr);
	}

	void OnBackBufferReadyToPresentThunk(SWindow& Window, const FTextureRHIRef& Texture)
	{
		OnBackBufferReadyToPresentDelegate.Broadcast(Window, Texture);
	}

	void OnAddBackBufferReadyToPresentPassThunk(FRDGBuilder& GraphBuilder, SWindow& Window, FRDGTexture* BackBuffer)
	{
		OnAddBackBufferReadyToPresentPassDelegate.Broadcast(GraphBuilder, Window, BackBuffer);
	}

	TSharedRef<FSlateRenderer> UnderlyingRenderer;

	FDelegateHandle OnSlateWindowRenderedHandle;
	FDelegateHandle OnSlateWindowDestroyedHandle;
	FDelegateHandle OnPreResizeWindowBackBufferHandle;
	FDelegateHandle OnPostResizeWindowBackBufferHandle;
	FDelegateHandle OnBackBufferReadyToPresentHandle;
	FDelegateHandle OnAddBackBufferReadyToPresentPassHandle;
};
