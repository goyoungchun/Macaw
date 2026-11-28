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

        Int32 GetTileX() const;
        bool SetTileX(Int32 X);
        Int32 GetTileY() const;
        bool SetTileY(Int32 Y);

        const Core::TArray<Engine::FLandscapeSection>& GetSections() const;
        void SetSections(Core::TArray<Engine::FLandscapeSection> Sections);

    private:
        UPROPERTY(Edit, Serialize, Category = "Landscape", Getter = GetTileX, Setter = SetTileX)
        Int32 mTileX{};

        UPROPERTY(Edit, Serialize, Category = "Landscape", Getter = GetTileY, Setter = SetTileY)
        Int32 mTileY{};

        UPROPERTY(Serialize)
        Core::TArray<Engine::FLandscapeSection> mSections{};
    };

}
