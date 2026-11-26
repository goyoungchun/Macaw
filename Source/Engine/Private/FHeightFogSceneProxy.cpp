/**
 * @file FHeightFogSceneProxy.cpp
 * @brief 컴포넌트에서 렌더 장면으로 복사하는 안개 설정과 에디터 아이콘의 비소유 참조입니다.
 * @ingroup Engine
 */

#include "Engine/FHeightFogSceneProxy.h"
#include "Core/Literals/UnitLiterals.h"
#include "Engine/FPrimitiveSceneProxy.h"
#include "Core/Math/FTransform.h"

using namespace Core::Literals;

namespace Engine {

    constinit const Core::FInverseLength Engine::FHeightFogSceneProxy::mDefaultDensity{0.02f / 1_m};
    constinit const Core::FInverseLength Engine::FHeightFogSceneProxy::mDefaultHeightFalloff{0.2f / 1_m};
    constinit const Core::FLength Engine::FHeightFogSceneProxy::mDefaultEndDistance{1000_m};

    /**
     * @brief 안개 위치와 아이콘으로 카메라를 향하는 에디터 빌보드 프록시를 구성합니다.
     */
    Engine::FPrimitiveSceneProxy Engine::FHeightFogSceneProxy::CreateIconProxy() const {
        Engine::FPrimitiveSceneProxy Proxy{};
        Proxy.mId = mId;
        Proxy.mWorld = FTransform{mPosition, {}, {1.0f, 1.0f, 1.0f}}.ToMatrix();
        Proxy.mSprite = Engine::FDrawSprite{mIcon, {1.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, true};

        return Proxy;
    }

}
