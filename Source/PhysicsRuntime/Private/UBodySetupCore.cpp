/**
 * @file UBodySetupCore.cpp
 * @brief 바디 셋업이 연결될 본을 식별하는 기반 클래스입니다.
 * @ingroup PhysicsRuntime
 */

#include "Core/Types.h"
#include "PhysicsRuntime/UBodySetupCore.h"

namespace PhysicsRuntime {

    /**
     * @brief 본에 연결되지 않은 바디 셋업을 구성합니다.
     */
    PhysicsRuntime::UBodySetupCore::UBodySetupCore() = default;

    /**
     * @brief 객체의 수명을 끝내고 소유한 리소스를 정리합니다.
     */
    PhysicsRuntime::UBodySetupCore::~UBodySetupCore() = default;

    /**
     * @brief 바디가 연결될 본 이름을 반환합니다.
     */
    const Core::FString& PhysicsRuntime::UBodySetupCore::GetBoneName() const {
        return mBoneName;
    }

    /**
     * @brief 바디가 연결될 본 이름을 변경합니다.
     */
    void PhysicsRuntime::UBodySetupCore::SetBoneName(const Core::FString& BoneName) {
        mBoneName = BoneName;
    }

}
