/**
 * @file UDirectionalLightComponent.cpp
 * @brief 방향광과 캐스케이드 그림자 설정을 관리합니다.
 * @ingroup Engine
 */

#include "Engine/Component/UDirectionalLightComponent.h"
#include "Core/Literals/AngleFunctions.h"
#include "Core/Archive.h"

#include <cmath>

using namespace Core::Literals;

namespace Engine {

    /**
     * @brief UDirectionalLightComponent의 기본 상태를 초기화합니다.
     */
    Engine::UDirectionalLightComponent::UDirectionalLightComponent()
		: mShadowDistance{100_m},
		  mSourceAngle{0.5357_deg} {
    }

    /**
     * @brief UDirectionalLightComponent 객체의 수명을 끝내고 소유 자원을 정리합니다.
     */
    Engine::UDirectionalLightComponent::~UDirectionalLightComponent() = default;

    /**
     * @brief 현재 컴포넌트의 조명·그림자 설정으로 렌더 프록시를 생성합니다.
     */
    Engine::FLightSceneProxy Engine::UDirectionalLightComponent::CreateLightProxy() const {
        Engine::FLightSceneProxy Proxy{Engine::ULightComponent::CreateLightProxy()};
        Proxy.mType = Engine::ELightType::Directional;
        Proxy.mAtmosphereSunLight = mAtmosphereSunLight;
        Proxy.mCascadeCount = mCascadeCount;
        Proxy.mShadowDistance = mShadowDistance;
        Proxy.mCascadeTransitionFraction = mCascadeTransitionFraction;
        Proxy.mShadowSourceAngle = GetShadowSourceAngle();

        return Proxy;
    }

    /**
     * @brief 방향광 그림자의 캐스케이드 개수를 반환합니다.
     */
    Uint32 Engine::UDirectionalLightComponent::GetCascadeCount() const {
        return mCascadeCount;
    }

    /**
     * @brief 방향광 그림자의 캐스케이드 개수를 검증하고 변경합니다.
     */
    bool Engine::UDirectionalLightComponent::SetCascadeCount(Uint32 Count) {
        if (Count < 1 || Count > 4) {
            return false;
        }

        mCascadeCount = Count;

        SynchronizeLight();

        return true;
    }

    /**
     * @brief 방향광 그림자의 최대 표시 거리를 반환합니다.
     */
    Core::FLength Engine::UDirectionalLightComponent::GetShadowDistance() const {
        return mShadowDistance;
    }

    /**
     * @brief 방향광 그림자의 최대 표시 거리를 검증하고 변경합니다.
     */
    bool Engine::UDirectionalLightComponent::SetShadowDistance(Core::FLength Distance) {
        if (!Distance.IsFinite() || Distance < 1_m || Distance > 100000_m) {
            return false;
        }

        mShadowDistance = Distance;

        SynchronizeLight();

        return true;
    }

    /**
     * @brief 각 캐스케이드 구간에서 전환에 사용하는 비율을 반환합니다.
     */
    float Engine::UDirectionalLightComponent::GetCascadeTransitionFraction() const {
        return mCascadeTransitionFraction;
    }

    /**
     * @brief 각 캐스케이드 구간에서 전환에 사용하는 비율을 검증하고 변경합니다.
     */
    bool Engine::UDirectionalLightComponent::SetCascadeTransitionFraction(float Fraction) {
        if (!std::isfinite(Fraction) || Fraction < 0.0f || Fraction > 0.5f) {
            return false;
        }

        mCascadeTransitionFraction = Fraction;

        SynchronizeLight();

        return true;
    }

    Core::FAngle Engine::UDirectionalLightComponent::GetSourceAngle() const {
        return mSourceAngle;
    }

    bool Engine::UDirectionalLightComponent::SetSourceAngle(Core::FAngle Angle) {
        if (!Angle.IsFinite() || Angle < 0_deg || Angle > 30_deg) {
            return false;
        }

        mSourceAngle = Angle;
        SynchronizeLight();

        return true;
    }

    /**
     * @brief PCSS 반그림자 계산에 쓰는 광원 각반지름을 반환합니다.
     */
    Core::FAngle Engine::UDirectionalLightComponent::GetShadowSourceAngle() const {
        return mSourceAngle * 0.5f;
    }

    /**
     * @brief PCSS 반그림자 계산에 쓰는 광원 각반지름을 검증하고 변경합니다.
     */
    bool Engine::UDirectionalLightComponent::SetShadowSourceAngle(Core::FAngle Angle) {
        return SetSourceAngle(Angle * 2.0f);
    }

    bool Engine::UDirectionalLightComponent::IsAtmosphereSunLight() const {
        return mAtmosphereSunLight;
    }

    bool Engine::UDirectionalLightComponent::SetAtmosphereSunLight(bool Enabled) {
        if (mAtmosphereSunLight != Enabled) {
            mAtmosphereSunLight = Enabled;
            SynchronizeLight();
        }

        return true;
    }

    void Engine::UDirectionalLightComponent::Serialize(Core::FArchive& Archive) {
        Engine::ULightComponent::Serialize(Archive);

        if (Archive.IsLoading() && !Archive.HasError() && !Archive.HasField("SourceAngle") && Archive.HasField("ShadowSourceAngle")) {
            Core::FAngle LegacyRadius{};
            Archive.Serialize("ShadowSourceAngle", LegacyRadius, Core::QuantityUnits::Degree);

            if (!Archive.HasError() && !SetShadowSourceAngle(LegacyRadius)) {
                Archive.SetError("Cannot apply property: ShadowSourceAngle");
            }
        }
    }

}
