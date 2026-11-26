/**
 * @file PrimitiveBodySetup.h
 * @brief Plane, Cube와 Sphere 기본 메시가 공유하는 충돌 형상을 제공합니다.
 * @ingroup Engine
 */

#pragma once

#include "Engine/EPrimitiveType.h"

#include <memory>

namespace PhysicsRuntime {
    class UBodySetup;
}

namespace Engine {

    /**
     * @brief 기본 프리미티브 종류별로 공유하는 읽기 전용 바디 셋업을 반환합니다.
     * @details 형상 크기는 기본 메시의 경계와 같습니다. Cube는 한 변 1의 상자, Sphere는 반지름 0.5의 구이며,
     *          두께가 없는 Plane은 얇은 상자로 근사합니다.
     * @param[in] Type 기본 프리미티브 종류입니다.
     * @return 컴포넌트들이 소유권을 공유하는 바디 셋업이며, 유효하지 않은 종류이면 비어 있습니다.
     */
    std::shared_ptr<const PhysicsRuntime::UBodySetup> AcquirePrimitiveBodySetup(Engine::EPrimitiveType Type);

}
