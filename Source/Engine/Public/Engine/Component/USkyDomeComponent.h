#pragma once

#include "CoreUObject/TObjectRef.h"
#include "Engine/Component/USceneComponent.h"
#include "USkyDomeComponent.generated.h"

namespace Asset {
    class UTexture2D;
}

namespace Engine {

    UCLASS()
    class USkyDomeComponent final : public Engine::USceneComponent {
        GENERATED_BODY()

    public:
        USkyDomeComponent() = default;
        ~USkyDomeComponent() override;

    public:
        const Core::FString& GetPanoramaPath() const;
        bool SetPanoramaPath(const Core::FString& Path);

        bool SetTransform(const FTransform& Transform) override;
        void UnregisterComponent() override;

        void ResolvePanorama();
        void SynchronizeSkyDome();

    private:
        void OnRegister() override;
        void OnOwnerVisibilityChanged() override;

        Asset::UTexture2D* LoadPanorama(const Core::FString& Path) const;

    private:
        UPROPERTY(Edit, Serialize, Category = "Settings", Name = "Panorama", Getter = GetPanoramaPath, Setter = SetPanoramaPath, Meta = (AssetType = "Texture"))
        Core::FString mPanoramaPath{"/Engine/Sky/Epic_BlueSunset_EquiRect_flat.dds.masset"};

        CoreUObject::TObjectRef<Asset::UTexture2D> mPanorama{};
    };

}
