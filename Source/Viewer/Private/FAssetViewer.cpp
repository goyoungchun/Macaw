/**
 * @file FAssetViewer.cpp
 * @brief 에셋별 콘텐츠의 카메라, 렌더 타깃, Fit과 이미지 캡처를 공통으로 관리합니다.
 * @ingroup Viewer
 */

#include "Viewer/FAssetViewer.h"

#include "Viewer/FMeshViewerContent.h"
#include "Viewer/FAnimationViewerContent.h"
#include "Asset/Animation/UAnimSequence.h"
#include "CoreUObject/Reflection/Cast.h"
#include "Viewer/FViewerCapture.h"
#include "Viewer/IViewerContent.h"
#include "Asset/UTexture2D.h"
#include "Engine/FPrimitiveDrawInterface.h"
#include "Engine/FRenderTarget.h"
#include "Engine/FSceneViewFamily.h"
#include "RenderCore/IRendererModule.h"

#include <algorithm>
#include <cmath>

using namespace Viewer;

FAssetViewer::FAssetViewer(RenderCore::IRendererModule* Renderer, Asset::FAssetManager* Assets, bool ShowEnvironment)
	: mRenderer{Renderer},
	  mAssets{Assets},
	  mViewMode{ShowEnvironment ? Engine::EViewMode::Lit : Engine::EViewMode::Unlit},
	  mShowEnvironment{ShowEnvironment} {
    mShowFlags.mSky = ShowEnvironment;
    mShowFlags.mFog = false;
    mShowFlags.mLightIcons = false;
    mShowFlags.mFogIcons = false;
    mShowFlags.mLightVisualizers = false;
    mShowFlags.mSprites = false;
    mShowFlags.mText = false;
}

FAssetViewer::~FAssetViewer() = default;

bool FAssetViewer::Supports(Asset::EAssetType Type) {
    return Type == Asset::EAssetType::Mesh || Type == Asset::EAssetType::SkeletalMesh || Type == Asset::EAssetType::Material || Type == Asset::EAssetType::Skeleton || Type == Asset::EAssetType::Texture || Type == Asset::EAssetType::AnimSequence;
}

bool FAssetViewer::SetAsset(CoreUObject::UObject* Object, Asset::EAssetType Type) {
    mError.clear();

    if (Object == nullptr || !Supports(Type)) {
        mError = "The asset is null or its type has no viewer";

        return false;
    }

    std::unique_ptr<IViewerContent> Content{};

    if (Type == Asset::EAssetType::AnimSequence) {
        auto* Sequence{CoreUObject::Cast<Asset::UAnimSequence>(Object)};

        if (Sequence == nullptr) {
            mError = "The asset does not match the specified type";

            return false;
        }

        auto Animation{std::make_unique<FAnimationViewerContent>(mRenderer, mAssets)};
        Animation->SetAnimation(Sequence);
        Content = std::move(Animation);
    } else {
        auto Mesh{std::make_unique<FMeshViewerContent>(mRenderer, mAssets)};

        if (!Mesh->SetAsset(Object, Type, mError)) {
            return false;
        }

        Content = std::move(Mesh);
    }

    return SetContent(std::move(Content));
}

const Core::FString& FAssetViewer::GetError() const {
    return mError;
}

bool FAssetViewer::SetContent(std::unique_ptr<IViewerContent> Content) {
    if (Content == nullptr) {
        return false;
    }

    mContent = std::move(Content);
    mDisplayTexture.reset();
    mDisplaySource = nullptr;
    mCamera = FViewerCamera{};
    mImagePadding = 1.0f;

    return true;
}

void FAssetViewer::Close(bool ReleaseTarget) {
    mDisplayTexture.reset();
    mDisplaySource = nullptr;
    mContent.reset();

    if (ReleaseTarget) {
        mTarget.reset();
    }
}

IViewerContent* FAssetViewer::GetContent() const {
    return mContent.get();
}

bool FAssetViewer::Resize(Uint32 Width, Uint32 Height) {
    if (Width == 0 || Height == 0 || Width > 16384 || Height > 16384) {
        return false;
    }

    if (mRenderer == nullptr) {
        return false;
    }

    if (mContent == nullptr || mContent->GetTexture() == nullptr) {
        if (mTarget == nullptr) {
            mTarget = mRenderer->CreateRenderTarget(Width, Height);
        } else if ((mTarget->GetWidth() != Width || mTarget->GetHeight() != Height) && !mRenderer->ResizeRenderTarget(mTarget.get(), Width, Height)) {
            return false;
        }

        if (mTarget == nullptr) {
            return false;
        }
    }

    mSize = {static_cast<float>(Width), static_cast<float>(Height)};

    return true;
}

bool FAssetViewer::Fit(float Padding) {
    if (mContent == nullptr || !mContent->IsReady() || !std::isfinite(Padding) || Padding < 1.0f) {
        return false;
    }

    mContent->Synchronize();

    if (mContent->GetTexture() != nullptr) {
        mImagePadding = Padding;

        return true;
    }

    return mCamera.Fit(mContent->GetBounds(), mSize, Padding);
}

EViewerPrepareResult FAssetViewer::PrepareCapture(const FViewerCaptureState& State, Core::FString& Error) {
    Error.clear();

    if (mContent == nullptr) {
        Error = "No viewer content is open";

        return EViewerPrepareResult::Failed;
    }

    return mContent->PrepareCapture(State, Error);
}

void FAssetViewer::Tick(float DeltaTime) {
    if (mContent != nullptr && std::isfinite(DeltaTime) && DeltaTime >= 0.0f) {
        mContent->Tick(DeltaTime);
    }
}

EViewerPrepareResult FAssetViewer::PrepareCapture(Core::FString& Error) {
    return PrepareCapture(mContent != nullptr ? mContent->GetCaptureState() : FViewerCaptureState{}, Error);
}

bool FAssetViewer::Render(const Engine::FPrimitiveDrawInterface* EditorElements, const Engine::FPrimitiveDrawInterface* BackgroundOverlay, bool CheckerboardBackground) {
    if (mRenderer == nullptr || mContent == nullptr || !mContent->IsReady()) {
        return false;
    }

    mContent->Synchronize();

    if (mContent->GetTexture() != nullptr) {
        return true;
    }

    if (!Resize(static_cast<Uint32>(mSize.x), static_cast<Uint32>(mSize.y)) || mContent->GetScene() == nullptr) {
        return false;
    }

    Engine::FPrimitiveDrawInterface ContentOverlay{};
    mContent->BuildOverlay(ContentOverlay, mCamera.GetRadius());

    Engine::FSceneViewFamily Family{mContent->GetScene(), mTarget.get(), mCamera.CalcSceneView(mSize), EditorElements, mViewMode, mShowFlags, mGridSpacing};
    Family.mBackgroundOverlay = BackgroundOverlay != nullptr ? BackgroundOverlay : &ContentOverlay;
    Family.mCheckerboardBackground = CheckerboardBackground;

    if (mShowEnvironment) {
        mContent->PrepareEnvironment(Family);
    }

    mRenderer->BeginRenderingViewFamily(Family);

    return true;
}

std::unique_ptr<FViewerCapture> FAssetViewer::Capture(Core::FString& Error, bool CheckerboardBackground) {
    Error.clear();

    if (!Render(nullptr, nullptr, CheckerboardBackground)) {
        Error = "Cannot render the viewer capture";

        return nullptr;
    }

    Asset::UTexture2D* Source{mContent->GetTexture()};
    auto Texture{Source != nullptr ? mRenderer->CaptureTexture(*Source, static_cast<Uint32>(mSize.x), static_cast<Uint32>(mSize.y), GetImageRect(), Error, CheckerboardBackground) : mRenderer->CaptureRenderTarget(*mTarget, Error)};

    return Texture != nullptr ? std::make_unique<FViewerCapture>(std::move(Texture)) : nullptr;
}

FViewerCamera& FAssetViewer::GetCamera() {
    return mCamera;
}

const FViewerCamera& FAssetViewer::GetCamera() const {
    return mCamera;
}

Engine::FEngineShowFlags& FAssetViewer::GetShowFlags() {
    return mShowFlags;
}

void FAssetViewer::SetViewMode(Engine::EViewMode Mode) {
    mViewMode = Mode;
}

void FAssetViewer::SetGridSpacing(float Spacing) {
    mGridSpacing = std::isfinite(Spacing) ? std::max(Spacing, 0.0f) : 0.0f;
}

FVector2 FAssetViewer::GetSize() const {
    return mSize;
}

FVector4 FAssetViewer::GetImageRect() const {
    const Asset::UTexture2D* Texture{mContent != nullptr ? mContent->GetTexture() : nullptr};

    if (Texture == nullptr || Texture->GetSizeX() == 0 || Texture->GetSizeY() == 0) {
        return {0.0f, 0.0f, mSize.x, mSize.y};
    }

    const float Scale{std::min(mSize.x / static_cast<float>(Texture->GetSizeX()), mSize.y / static_cast<float>(Texture->GetSizeY())) / mImagePadding};
    const float Width{std::min(mSize.x, static_cast<float>(Texture->GetSizeX()) * Scale)};
    const float Height{std::min(mSize.y, static_cast<float>(Texture->GetSizeY()) * Scale)};

    return {(mSize.x - Width) * 0.5f, (mSize.y - Height) * 0.5f, Width, Height};
}

void* FAssetViewer::GetNativeTexture() const {
    const Asset::UTexture2D* Texture{mContent != nullptr ? mContent->GetTexture() : nullptr};

    if (Texture == nullptr) {
        return mTarget != nullptr ? mTarget->GetNativeTexture() : nullptr;
    }

    // sRGB 텍스처의 리소스 뷰는 샘플링할 때 선형으로 디코딩하므로 UI가 그대로 그리면 어둡게 보인다.
    // 렌더러의 텍스처 복사(디코딩 후 다시 인코딩)로 저장된 색 그대로의 UNORM 이미지를 만들어 보여 준다.
    // 형식이 sRGB 변환 없는 같은 계열의 뷰를 만들 수 없는 typed 리소스이므로 뷰 대체로는 해결할 수 없다.
    if (!Texture->IsSRGB() || mRenderer == nullptr || !Texture->HasResources()) {
        return Texture->GetShaderResourceView();
    }

    if (mDisplayTexture == nullptr || mDisplaySource != Texture) {
        // 큰 텍스처는 화면에 보이는 크기보다 훨씬 크므로 한 변을 2048 이하로 줄여 만든다.
        constexpr float MaxSide{2048.0f};
        const float SizeX{static_cast<float>(Texture->GetSizeX())};
        const float SizeY{static_cast<float>(Texture->GetSizeY())};
        const float Scale{std::min(1.0f, MaxSide / std::max(SizeX, SizeY))};
        const float Width{std::max(1.0f, std::floor(SizeX * Scale))};
        const float Height{std::max(1.0f, std::floor(SizeY * Scale))};
        Core::FString Error{};

        mDisplayTexture = mRenderer->CaptureTexture(*Texture, static_cast<Uint32>(Width), static_cast<Uint32>(Height), {0.0f, 0.0f, Width, Height}, Error);
        mDisplaySource = Texture;
    }

    return mDisplayTexture != nullptr ? mDisplayTexture->GetShaderResourceView() : Texture->GetShaderResourceView();
}
