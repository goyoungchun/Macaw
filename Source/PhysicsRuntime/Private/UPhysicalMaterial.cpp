/**
 * @file UPhysicalMaterial.cpp
 * @brief 충돌 형상에 적용할 마찰과 반발 계수를 관리합니다.
 * @ingroup PhysicsRuntime
 */

#include "Core/Types.h"
#include "PhysicsRuntime/UPhysicalMaterial.h"

#include <cmath>

namespace PhysicsRuntime {

    /**
     * @brief 정지 마찰 계수를 반환합니다.
     */
    float PhysicsRuntime::UPhysicalMaterial::GetStaticFriction() const {
        return mStaticFriction;
    }

    /**
     * @brief 정지 마찰 계수를 검증하고 변경합니다.
     */
    bool PhysicsRuntime::UPhysicalMaterial::SetStaticFriction(float Friction) {
        if (!std::isfinite(Friction) || Friction < 0.0f) {
            return false;
        }

        mStaticFriction = Friction;

        return true;
    }

    /**
     * @brief 운동 마찰 계수를 반환합니다.
     */
    float PhysicsRuntime::UPhysicalMaterial::GetDynamicFriction() const {
        return mDynamicFriction;
    }

    /**
     * @brief 운동 마찰 계수를 검증하고 변경합니다.
     */
    bool PhysicsRuntime::UPhysicalMaterial::SetDynamicFriction(float Friction) {
        if (!std::isfinite(Friction) || Friction < 0.0f) {
            return false;
        }

        mDynamicFriction = Friction;

        return true;
    }

    /**
     * @brief 반발 계수를 반환합니다.
     */
    float PhysicsRuntime::UPhysicalMaterial::GetRestitution() const {
        return mRestitution;
    }

    /**
     * @brief 반발 계수를 검증하고 변경합니다.
     */
    bool PhysicsRuntime::UPhysicalMaterial::SetRestitution(float Restitution) {
        if (!std::isfinite(Restitution) || Restitution < 0.0f || Restitution > 1.0f) {
            return false;
        }

        mRestitution = Restitution;

        return true;
    }

}
