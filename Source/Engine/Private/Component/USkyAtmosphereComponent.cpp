#include "Engine/Component/USkyAtmosphereComponent.h"

#include "Engine/FSceneInterface.h"
#include "Engine/FSkyAtmosphereSceneProxy.h"
#include "Engine/UWorld.h"

#include <cmath>

using namespace Core::Literals;

namespace Engine {

    Engine::USkyAtmosphereComponent::USkyAtmosphereComponent()
		: mGroundRadius{6360_km},
		  mAtmosphereHeight{100_km},
		  mRayleighScaleHeight{8_km},
		  mMieScaleHeight{1200_m} {
    }

    Engine::USkyAtmosphereComponent::~USkyAtmosphereComponent() {
        UnregisterComponent();
    }

    Core::FLength Engine::USkyAtmosphereComponent::GetGroundRadius() const {
        return mGroundRadius;
    }

    bool Engine::USkyAtmosphereComponent::SetGroundRadius(Core::FLength Value) {
        if (!std::isfinite(Value.GetBase()) || Value < 1_km) {
            return false;
        }

        mGroundRadius = Value;
        SynchronizeSkyAtmosphere();

        return true;
    }

    Core::FLength Engine::USkyAtmosphereComponent::GetAtmosphereHeight() const {
        return mAtmosphereHeight;
    }

    bool Engine::USkyAtmosphereComponent::SetAtmosphereHeight(Core::FLength Value) {
        if (!std::isfinite(Value.GetBase()) || Value < 1_m) {
            return false;
        }

        mAtmosphereHeight = Value;
        SynchronizeSkyAtmosphere();

        return true;
    }

    Core::FLength Engine::USkyAtmosphereComponent::GetRayleighScaleHeight() const {
        return mRayleighScaleHeight;
    }

    bool Engine::USkyAtmosphereComponent::SetRayleighScaleHeight(Core::FLength Value) {
        if (!std::isfinite(Value.GetBase()) || Value < 1_m) {
            return false;
        }

        mRayleighScaleHeight = Value;
        SynchronizeSkyAtmosphere();

        return true;
    }

    float Engine::USkyAtmosphereComponent::GetRayleighScatteringScale() const {
        return mRayleighScatteringScale;
    }

    bool Engine::USkyAtmosphereComponent::SetRayleighScatteringScale(float Value) {
        if (!std::isfinite(Value) || Value < 0.0f) {
            return false;
        }

        mRayleighScatteringScale = Value;
        SynchronizeSkyAtmosphere();

        return true;
    }

    Core::FLength Engine::USkyAtmosphereComponent::GetMieScaleHeight() const {
        return mMieScaleHeight;
    }

    bool Engine::USkyAtmosphereComponent::SetMieScaleHeight(Core::FLength Value) {
        if (!std::isfinite(Value.GetBase()) || Value < 1_m) {
            return false;
        }

        mMieScaleHeight = Value;
        SynchronizeSkyAtmosphere();

        return true;
    }

    float Engine::USkyAtmosphereComponent::GetMieScatteringScale() const {
        return mMieScatteringScale;
    }

    bool Engine::USkyAtmosphereComponent::SetMieScatteringScale(float Value) {
        if (!std::isfinite(Value) || Value < 0.0f) {
            return false;
        }

        mMieScatteringScale = Value;
        SynchronizeSkyAtmosphere();

        return true;
    }

    float Engine::USkyAtmosphereComponent::GetMieAnisotropy() const {
        return mMieAnisotropy;
    }

    bool Engine::USkyAtmosphereComponent::SetMieAnisotropy(float Value) {
        if (!std::isfinite(Value) || std::abs(Value) >= 1.0f) {
            return false;
        }

        mMieAnisotropy = Value;
        SynchronizeSkyAtmosphere();

        return true;
    }

    float Engine::USkyAtmosphereComponent::GetOzoneAbsorptionScale() const {
        return mOzoneAbsorptionScale;
    }

    bool Engine::USkyAtmosphereComponent::SetOzoneAbsorptionScale(float Value) {
        if (!std::isfinite(Value) || Value < 0.0f) {
            return false;
        }

        mOzoneAbsorptionScale = Value;
        SynchronizeSkyAtmosphere();

        return true;
    }

    bool Engine::USkyAtmosphereComponent::SetTransform(const FTransform& Transform) {
        if (!Engine::USceneComponent::SetTransform(Transform)) {
            return false;
        }

        SynchronizeSkyAtmosphere();

        return true;
    }

    void Engine::USkyAtmosphereComponent::UnregisterComponent() {
        if (GetWorld() != nullptr && GetWorld()->GetScene() != nullptr) {
            GetWorld()->GetScene()->RemoveSkyAtmosphere(GetGuid());
        }

        Engine::USceneComponent::UnregisterComponent();
    }

    void Engine::USkyAtmosphereComponent::SynchronizeSkyAtmosphere() {
        if (GetWorld() == nullptr || GetWorld()->GetScene() == nullptr) {
            return;
        }

        Engine::FSkyAtmosphereSceneProxy Proxy{};
        Proxy.mId = GetGuid();
        Proxy.mGroundPosition = GetTransform().mLocation;
        Proxy.mVisible = IsOwnerVisible();
        Proxy.mGroundRadius = mGroundRadius;
        Proxy.mAtmosphereHeight = mAtmosphereHeight;
        Proxy.mRayleighScaleHeight = mRayleighScaleHeight;
        Proxy.mRayleighScatteringScale = mRayleighScatteringScale;
        Proxy.mMieScaleHeight = mMieScaleHeight;
        Proxy.mMieScatteringScale = mMieScatteringScale;
        Proxy.mMieAnisotropy = mMieAnisotropy;
        Proxy.mOzoneAbsorptionScale = mOzoneAbsorptionScale;

        GetWorld()->GetScene()->AddSkyAtmosphere(Proxy);
    }

    void Engine::USkyAtmosphereComponent::OnRegister() {
        SynchronizeSkyAtmosphere();
    }

    void Engine::USkyAtmosphereComponent::OnOwnerVisibilityChanged() {
        SynchronizeSkyAtmosphere();
    }

}
