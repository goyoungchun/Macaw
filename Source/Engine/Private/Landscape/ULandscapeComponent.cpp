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

    const Core::TArray<Engine::FLandscapeSection>& ULandscapeComponent::GetSections() const {
        return mSections;
    }

    void ULandscapeComponent::SetSections(Core::TArray<Engine::FLandscapeSection> Sections) {
        mSections = std::move(Sections);
    }

}
