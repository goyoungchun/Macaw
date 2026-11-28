#include "Engine/Landscape/ULandscapeComponent.h"

#include "Asset/UStaticMesh.h"
#include "CoreUObject/Reflection/Cast.h"
#include "Engine/EPrimitiveType.h"
#include "Engine/Landscape/ALandscape.h"
#include "Engine/Landscape/FLandscapeComponentSceneProxy.h"

#include <utility>

namespace Engine {

    Engine::EPrimitiveType ULandscapeComponent::GetPrimitiveType() const {
        return Engine::EPrimitiveType::Count;
    }

    Asset::UStaticMesh* ULandscapeComponent::GetStaticMesh() const {
        return mRenderResources != nullptr ? mRenderResources->GetMesh() : nullptr;
    }

    const Asset::FMeshData& ULandscapeComponent::GetMeshData() const {
        static const Asset::FMeshData Empty{};
        const auto* Mesh{GetStaticMesh()};

        return Mesh != nullptr ? Mesh->GetMeshData() : Empty;
    }

    std::unique_ptr<Engine::FPrimitiveSceneProxy> ULandscapeComponent::CreateSceneProxy() const {
        if (!ShouldCreateRenderState()) {
            return {};
        }

        auto Proxy{std::make_unique<Engine::FLandscapeComponentSceneProxy>(mRenderResources)};
        InitializeSceneProxy(*Proxy);

        return Proxy;
    }

    bool ULandscapeComponent::ShouldCreateRenderState() const {
        return mRenderResources != nullptr;
    }

    Int32 ULandscapeComponent::GetTileX() const {
        return mTileX;
    }

    bool ULandscapeComponent::SetTileX(Int32 X) {
        if (X < 0) {
            return false;
        }

        mTileX = X;
        UpdateTileTransform();

        return true;
    }

    Int32 ULandscapeComponent::GetTileY() const {
        return mTileY;
    }

    bool ULandscapeComponent::SetTileY(Int32 Y) {
        if (Y < 0) {
            return false;
        }

        mTileY = Y;
        UpdateTileTransform();

        return true;
    }

    const Core::TArray<Engine::FLandscapeSection>& ULandscapeComponent::GetSections() const {
        return mSections;
    }

    void ULandscapeComponent::SetSections(Core::TArray<Engine::FLandscapeSection> Sections) {
        mSections = std::move(Sections);
    }

    void ULandscapeComponent::SetQuadsPerSide(Uint32 Quads) {
        mQuadsPerSide = Quads;
    }

    void ULandscapeComponent::OnRegister() {
        // 타일의 Quad 수를 따로 저장하기 전의 레벨은 메인 Landscape 설정과 저장된 섹션 수에서 복원합니다.
        if (mQuadsPerSide == 0) {
            const auto* Proxy{CoreUObject::Cast<Engine::ALandscapeStreamingProxy>(GetOwner())};
            const Engine::ALandscape* Landscape{Proxy != nullptr ? Proxy->GetLandscapeActor().Get() : CoreUObject::Cast<Engine::ALandscape>(GetOwner())};

            if (Landscape != nullptr) {
                const Uint32 SectionsPerSide{mSections.size() == 4 ? 2u : 1u};
                mQuadsPerSide = static_cast<Uint32>(Landscape->GetQuadsPerSection()) * SectionsPerSide;
            }
        }

        // 레벨에서 복원한 설정으로 런타임 격자와 높이맵을 다시 확보합니다.
        mRenderResources = Engine::FLandscapeRenderResources::Acquire(mQuadsPerSide);
        UpdateTileTransform();
    }

    void ULandscapeComponent::UpdateTileTransform() {
        if (GetOwner() == nullptr || mRenderResources == nullptr) {
            return;
        }

        // 전체 격자 기준 타일 좌표에 타일 크기를 곱하고 소유 프록시의 회전과 스케일을 적용합니다.
        const float TileSize{Engine::LandscapeQuadSize.GetBase() * mQuadsPerSide};
        FTransform Transform{GetOwner()->GetActorTransform()};
        Transform.mLocation = Transform.ToMatrix().TransformPosition({TileSize * mTileX, TileSize * mTileY, 0.0f});
        SetTransform(Transform);
    }

}
