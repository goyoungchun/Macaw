/**
 * @file FLightSceneProxy.cpp
 * @brief 렌더 장면에 복사하는 광원 상태와 에디터 아이콘의 비소유 참조입니다.
 * @ingroup Engine
 */

#include "Engine/FLightSceneProxy.h"
#include "Core/Literals/UnitLiterals.h"
#include "Engine/FPrimitiveSceneProxy.h"
#include "Core/Math/FTransform.h"

using namespace Core::Literals;

namespace Engine {

    constinit const Core::FLength Engine::FLightSceneProxy::mDefaultAttenuationRadius{10_m};
    constinit const Core::FAngle Engine::FLightSceneProxy::mDefaultInnerConeAngle{20_deg};
    constinit const Core::FAngle Engine::FLightSceneProxy::mDefaultOuterConeAngle{35_deg};
    constinit const Core::FLength Engine::FLightSceneProxy::mDefaultShadowSourceRadius{0.1_m};
    constinit const Core::FAngle Engine::FLightSceneProxy::mDefaultShadowSourceAngle{1_deg};
    constinit const Core::FLength Engine::FLightSceneProxy::mDefaultShadowDistance{100_m};

    /**
     * @brief 광원 아이콘을 에디터 빌보드 프리미티브로 구성합니다.
     */
    Engine::FPrimitiveSceneProxy Engine::FLightSceneProxy::CreateIconProxy() const {
        Engine::FPrimitiveSceneProxy Proxy{};
        Proxy.mId = mId;
        Proxy.mWorld = FTransform{mPosition, {}, {1.0f, 1.0f, 1.0f}}.ToMatrix();
        Proxy.mSprite = Engine::FDrawSprite{mIcon, {1.0f, 1.0f}, mColor, true};

        return Proxy;
    }

}
