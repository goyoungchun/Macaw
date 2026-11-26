/**
 * @file USpotLightComponent.cpp
 * @brief 내부·외부 원뿔각으로 제한되는 스포트라이트를 관리합니다.
 * @ingroup Engine
 */

#include "Engine/Component/USpotLightComponent.h"
#include "Core/Literals/AngleFunctions.h"

#include <algorithm>
#include <cmath>

using namespace Core::Literals;

namespace Engine {

    /**
     * @brief USpotLightComponent의 기본 상태를 초기화합니다.
     */
    Engine::USpotLightComponent::USpotLightComponent()
		: mInnerConeAngle{20_deg},
		  mOuterConeAngle{35_deg} {
    }

    /**
     * @brief USpotLightComponent 객체의 수명을 끝내고 소유 자원을 정리합니다.
     */
    Engine::USpotLightComponent::~USpotLightComponent() = default;

    /**
     * @brief 현재 컴포넌트의 조명·그림자 설정으로 렌더 프록시를 생성합니다.
     */
    Engine::FLightSceneProxy Engine::USpotLightComponent::CreateLightProxy() const {
        Engine::FLightSceneProxy Proxy{Engine::UPointLightComponent::CreateLightProxy()};
        Proxy.mType = Engine::ELightType::Spot;
        Proxy.mInnerConeAngle = mInnerConeAngle;
        Proxy.mOuterConeAngle = mOuterConeAngle;

        return Proxy;
    }

    /**
     * @brief 스포트라이트 내부 원뿔의 반각(도)를 반환합니다.
     */
    Core::FAngle Engine::USpotLightComponent::GetInnerConeAngle() const {
        return mInnerConeAngle;
    }

    /**
     * @brief 스포트라이트 내부 원뿔의 반각(도)를 검증하고 변경합니다.
     */
    bool Engine::USpotLightComponent::SetInnerConeAngle(Core::FAngle InnerConeAngle) {
        if (!InnerConeAngle.IsFinite() || InnerConeAngle < 0_deg || InnerConeAngle >= 89_deg) {
            return false;
        }

        mInnerConeAngle = InnerConeAngle;
        mOuterConeAngle = std::max(mOuterConeAngle, mInnerConeAngle);

        SynchronizeLight();

        return true;
    }

    /**
     * @brief 스포트라이트 외부 원뿔의 반각(도)를 반환합니다.
     */
    Core::FAngle Engine::USpotLightComponent::GetOuterConeAngle() const {
        return mOuterConeAngle;
    }

    /**
     * @brief 스포트라이트 외부 원뿔의 반각(도)를 검증하고 변경합니다.
     */
    bool Engine::USpotLightComponent::SetOuterConeAngle(Core::FAngle OuterConeAngle) {
        if (!OuterConeAngle.IsFinite() || OuterConeAngle < 0_deg || OuterConeAngle >= 89_deg) {
            return false;
        }

        mOuterConeAngle = OuterConeAngle;
        mInnerConeAngle = std::min(mInnerConeAngle, mOuterConeAngle);

        SynchronizeLight();

        return true;
    }

}
