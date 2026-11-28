#pragma once

#include "Engine/Component/UPrimitiveComponent.h"
#include "Engine/Landscape/FLandscapeSection.h"
#include "ULandscapeComponent.generated.h"

namespace Engine {

    UCLASS()
    class ULandscapeComponent final : public Engine::UPrimitiveComponent {
        GENERATED_BODY()

    public:
        Engine::EPrimitiveType GetPrimitiveType() const override;
        const Asset::FMeshData& GetMeshData() const override;

        bool ShouldCreateRenderState() const override;

        const Core::TArray<Engine::FLandscapeSection>& GetSections() const;
        void SetSections(Core::TArray<Engine::FLandscapeSection> Sections);

    private:
        UPROPERTY(Serialize)
        Core::TArray<Engine::FLandscapeSection> mSections{};
    };

}
