#include "Engine/Landscape/ULandscapeComponent.h"

#include "Asset/FMeshData.h"
#include "Engine/EPrimitiveType.h"

#include <utility>

namespace Engine {

    Engine::EPrimitiveType ULandscapeComponent::GetPrimitiveType() const {
        return Engine::EPrimitiveType::Count;
    }

    const Asset::FMeshData& ULandscapeComponent::GetMeshData() const {
        static const Asset::FMeshData MeshData{};

        return MeshData;
    }

    bool ULandscapeComponent::ShouldCreateRenderState() const {
        return false;
    }

    Int32 ULandscapeComponent::GetTileX() const {
        return mTileX;
    }

    bool ULandscapeComponent::SetTileX(Int32 X) {
        if (X < 0) {
            return false;
        }

        mTileX = X;

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

        return true;
    }

    const Core::TArray<Engine::FLandscapeSection>& ULandscapeComponent::GetSections() const {
        return mSections;
    }

    void ULandscapeComponent::SetSections(Core::TArray<Engine::FLandscapeSection> Sections) {
        mSections = std::move(Sections);
    }

}
