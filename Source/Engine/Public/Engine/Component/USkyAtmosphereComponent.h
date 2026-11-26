#pragma once

#include "Core/Literals/UnitLiterals.h"
#include "Engine/Component/USceneComponent.h"
#include "USkyAtmosphereComponent.generated.h"

namespace Engine {

    UCLASS()
    class USkyAtmosphereComponent final : public Engine::USceneComponent {
        GENERATED_BODY()

    public:
        USkyAtmosphereComponent();
        ~USkyAtmosphereComponent() override;

    public:
        Core::FLength GetGroundRadius() const;
        bool SetGroundRadius(Core::FLength Value);

        Core::FLength GetAtmosphereHeight() const;
        bool SetAtmosphereHeight(Core::FLength Value);

        Core::FLength GetRayleighScaleHeight() const;
        bool SetRayleighScaleHeight(Core::FLength Value);

        float GetRayleighScatteringScale() const;
        bool SetRayleighScatteringScale(float Value);

        Core::FLength GetMieScaleHeight() const;
        bool SetMieScaleHeight(Core::FLength Value);

        float GetMieScatteringScale() const;
        bool SetMieScatteringScale(float Value);

        float GetMieAnisotropy() const;
        bool SetMieAnisotropy(float Value);

        float GetOzoneAbsorptionScale() const;
        bool SetOzoneAbsorptionScale(float Value);

        bool SetTransform(const FTransform& Transform) override;
        void UnregisterComponent() override;
        void SynchronizeSkyAtmosphere();

    private:
        void OnRegister() override;
        void OnOwnerVisibilityChanged() override;

    private:
        UPROPERTY(Edit, Serialize, Category = "Planet", Getter = GetGroundRadius, Setter = SetGroundRadius, Meta = (SerializeUnit = "km"))
        Core::FLength mGroundRadius{};

        UPROPERTY(Edit, Serialize, Category = "Planet", Getter = GetAtmosphereHeight, Setter = SetAtmosphereHeight, Meta = (SerializeUnit = "km"))
        Core::FLength mAtmosphereHeight{};

        UPROPERTY(Edit, Serialize, Category = "Rayleigh", Getter = GetRayleighScaleHeight, Setter = SetRayleighScaleHeight, Meta = (SerializeUnit = "km"))
        Core::FLength mRayleighScaleHeight{};

        UPROPERTY(Edit, Serialize, Category = "Rayleigh", Getter = GetRayleighScatteringScale, Setter = SetRayleighScatteringScale)
        float mRayleighScatteringScale{1.0f};

        UPROPERTY(Edit, Serialize, Category = "Mie", Getter = GetMieScaleHeight, Setter = SetMieScaleHeight, Meta = (SerializeUnit = "km"))
        Core::FLength mMieScaleHeight{};

        UPROPERTY(Edit, Serialize, Category = "Mie", Getter = GetMieScatteringScale, Setter = SetMieScatteringScale)
        float mMieScatteringScale{1.0f};

        UPROPERTY(Edit, Serialize, Category = "Mie", Getter = GetMieAnisotropy, Setter = SetMieAnisotropy)
        float mMieAnisotropy{0.8f};

        UPROPERTY(Edit, Serialize, Category = "Absorption", Getter = GetOzoneAbsorptionScale, Setter = SetOzoneAbsorptionScale)
        float mOzoneAbsorptionScale{1.0f};
    };

}
