/**
 * @file FPreviewScene.cpp
 * @brief 메인 장면과 독립된 EditorPreview 월드와 렌더 장면의 수명을 소유합니다.
 * @ingroup Viewer
 */

#include "Viewer/FPreviewScene.h"
#include "Core/Literals/UnitLiterals.h"

#include "Asset/UMaterial.h"
#include "Core/Math/FTransform.h"
#include "CoreUObject/Reflection/NewObject.h"
#include "Engine/FWorldContext.h"
#include "Engine/UWorld.h"
#include "Engine/FSceneInterface.h"
#include "Engine/FLightSceneProxy.h"
#include "Engine/FPrimitiveSceneProxy.h"
#include "Engine/FSceneViewFamily.h"
#include "Engine/PrimitiveMesh.h"
#include "RenderCore/IRendererModule.h"
#include "RenderCore/Shader/UShader.h"

#include <algorithm>
#include <cmath>

using namespace Core::Literals;

using namespace Viewer;

namespace {
    void IncludeFloorInClipRange(Engine::FSceneView& View, const FBox& FloorBounds) {
        const float DepthScale{View.mProjection(2, 2)};

        if (View.mOrthographic || DepthScale <= 1.0f) {
            return;
        }

        const FVector FarCorner{View.mForward.x >= 0.0f ? FloorBounds.mMaximum.x : FloorBounds.mMinimum.x, View.mForward.y >= 0.0f ? FloorBounds.mMaximum.y : FloorBounds.mMinimum.y, View.mForward.z >= 0.0f ? FloorBounds.mMaximum.z : FloorBounds.mMinimum.z};
        const Core::FLength FloorDepth{Core::FLength::FromBase((FarCorner - View.mLocation).Dot(View.mForward))};
        const Core::FLength DepthOffset{Core::FLength::FromBase(-View.mProjection(3, 2))};
        const Core::FLength FarPlane{DepthOffset / (DepthScale - 1.0f)};

        if (FloorDepth.IsFinite() && FloorDepth >= FarPlane) {
            const Core::FLength NearPlane{DepthOffset / DepthScale};
            const float Aspect{std::max(View.mSize.x, 1.0f) / std::max(View.mSize.y, 1.0f)};

            View.mProjection = FMatrix::Perspective(View.mFieldOfView, Aspect, NearPlane, FloorDepth * 1.1f);
            View.mViewProjection = View.mView * View.mProjection;
        }
    }
}

FPreviewScene::FPreviewScene(RenderCore::IRendererModule* Renderer, Asset::FAssetManager* Assets)
	: mRenderer{Renderer},
	  mAssets{Assets} {
}

FPreviewScene::~FPreviewScene() = default;

bool FPreviewScene::Initialize(Core::FString& Error) {
    if (mWorldContext != nullptr) {
        return true;
    }

    auto Context{std::make_unique<Engine::FWorldContext>(Engine::EWorldType::EditorPreview)};
    Engine::UWorld* World{Context->GetWorld()};
    World->SetAssets(mAssets);

    if (mRenderer != nullptr) {
        auto Scene{mRenderer->AllocateScene()};

        if (Scene == nullptr || !World->AttachScene(std::move(Scene))) {
            Error = "Cannot create the preview scene";

            return false;
        }

        Engine::FLightSceneProxy Ambient{};
        Ambient.mIntensity = 0.5f;
        World->GetScene()->AddLight(Ambient);

        Engine::FLightSceneProxy Directional{};
        Directional.mType = Engine::ELightType::Directional;
        Directional.mDirection = FVector{0.35f, 0.6f, -1.0f}.Normalized();
        Directional.mIntensity = 0.8f;
        Directional.mShadowBiasPreset = Engine::EShadowBiasPreset::Level10;
        Directional.mShadowSlopeBiasPreset = Engine::EShadowBiasPreset::Level10;
        World->GetScene()->AddLight(Directional);
    }

    mWorldContext = std::move(Context);

    return true;
}

void FPreviewScene::PrepareEnvironment(Engine::FSceneViewFamily& Family, const FBox& Bounds) {
    Engine::FSceneInterface* Scene{GetScene()};

    if (Scene == nullptr) {
        return;
    }

    Family.mPreviewSky = true;

    const FVector Size{Bounds.mMaximum - Bounds.mMinimum};
    const FVector Center{Bounds.mMinimum + Size * 0.5f};
    const float AssetSize{std::max({Size.x, Size.y, Size.z, (0.02_m).GetBase()})};
    const float Width{AssetSize * 8.0f};
    const float Thickness{AssetSize * 0.18f};

    if (!Center.IsFinite() || !Size.IsFinite() || !std::isfinite(Width) || Size.x < 0.0f || Size.y < 0.0f || Size.z < 0.0f) {
        return;
    }

    const float FloorTop{Bounds.mMinimum.z - AssetSize * 0.001f};
    const FMatrix Transform{FTransform{{Center.x, Center.y, FloorTop - Thickness * 0.5f}, {}, {Width, Width, Thickness}}.ToMatrix()};
    const FBox FloorBounds{Engine::GetPrimitiveMesh(Engine::EPrimitiveType::Cube).GetBounds().Transformed(Transform)};

    IncludeFloorInClipRange(Family.mView, FloorBounds);

    if (mFloorMaterial != nullptr && mFloorTransform == Transform) {
        return;
    }

    if (mFloorMaterial == nullptr) {
        static const auto Shader{[] {
            auto FloorShader{CoreUObject::NewObject<RenderCore::UShader>()};
            FloorShader->SetSourcePath("/Engine/Shaders/PreviewFloor.hlsl");
            FloorShader->SetWritesGBuffer(true);

            return FloorShader;
        }()};

        mFloorMaterial = CoreUObject::NewObject<Asset::UMaterial>();
        mFloorMaterial->SetShader(Shader.get());
        mFloorMaterial->SetBaseColor({0.55f, 0.58f, 0.63f, 1.0f});
        mFloorMaterial->SetSpecularStrength(0.0f);

        Engine::FPrimitiveSceneProxy Floor{};
        Floor.mId = mFloorId;
        Floor.mPrimitive = Engine::EPrimitiveType::Cube;
        Floor.mWorld = Transform;
        Floor.mBounds = FloorBounds;
        Floor.mMaterial = mFloorMaterial.get();
        Floor.mCastShadow = false;
        Floor.mReceiveShadow = true;
        Floor.mReceivesDecals = false;
        Scene->AddPrimitive(Floor);
    } else {
        Scene->UpdatePrimitiveTransform(mFloorId, Transform, FloorBounds);
    }

    mFloorTransform = Transform;
}

void FPreviewScene::Synchronize() {
    if (Engine::UWorld* World{GetWorld()}) {
        World->SendAllEndOfFrameUpdates();
    }
}

Engine::UWorld* FPreviewScene::GetWorld() const {
    return mWorldContext != nullptr ? mWorldContext->GetWorld() : nullptr;
}

Engine::FSceneInterface* FPreviewScene::GetScene() const {
    const Engine::UWorld* World{GetWorld()};

    return World != nullptr ? World->GetScene() : nullptr;
}
