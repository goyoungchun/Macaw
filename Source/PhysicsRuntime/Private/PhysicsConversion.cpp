/**
 * @file PhysicsConversion.cpp
 * @brief 엔진 수학 타입과 PhysX 수학 타입 사이의 변환을 제공합니다.
 * @ingroup PhysicsRuntime
 */

#include "Core/Types.h"
#include "Core/Literals/UnitLiterals.h"
#include "PhysicsRuntime/PhysicsConversion.h"

using namespace Core::Literals;

namespace PhysicsRuntime {

    /**
     * @brief 엔진 벡터를 PhysX 벡터로 변환합니다.
     */
    physx::PxVec3 ToPxVec3(const FVector& Vector) {
        return physx::PxVec3{Vector.x, Vector.y, Vector.z};
    }

    /**
     * @brief PhysX 벡터를 엔진 벡터로 변환합니다.
     */
    FVector ToFVector(const physx::PxVec3& Vector) {
        return FVector{Vector.x, Vector.y, Vector.z};
    }

    /**
     * @brief 엔진 쿼터니언을 PhysX 쿼터니언으로 변환합니다.
     */
    physx::PxQuat ToPxQuat(const FQuat& Quat) {
        return physx::PxQuat{Quat.x, Quat.y, Quat.z, Quat.w};
    }

    /**
     * @brief PhysX 쿼터니언을 엔진 쿼터니언으로 변환합니다.
     */
    FQuat ToFQuat(const physx::PxQuat& Quat) {
        return FQuat{Quat.x, Quat.y, Quat.z, Quat.w};
    }

    /**
     * @brief 엔진 변환의 위치와 회전을 PhysX 변환으로 변환합니다.
     */
    physx::PxTransform ToPxTransform(const FTransform& Transform) {
        return physx::PxTransform{PhysicsRuntime::ToPxVec3(Transform.mLocation), PhysicsRuntime::ToPxQuat(Transform.mRotation.Normalized())};
    }

    /**
     * @brief PhysX 변환과 별도로 보관한 스케일로 엔진 변환을 만듭니다.
     */
    FTransform ToFTransform(const physx::PxTransform& Transform, const FVector& Scale) {
        return FTransform{PhysicsRuntime::ToFVector(Transform.p), PhysicsRuntime::ToFQuat(Transform.q), Scale};
    }

    /**
     * @brief 엔진의 로컬 Z축 캡슐을 PhysX의 로컬 X축 캡슐로 맞추는 회전을 반환합니다.
     */
    physx::PxQuat GetSphylBasis() {
        return physx::PxQuat{-(90_deg).GetBase(), physx::PxVec3{0.0f, 1.0f, 0.0f}};
    }

}
