/**
 * @file FKSphereElem.cpp
 * @brief 바디 셋업에 포함되는 구 충돌 형상입니다.
 * @ingroup PhysicsRuntime
 */

#include "Core/Types.h"
#include "PhysicsRuntime/FKSphereElem.h"
#include "Core/Literals/UnitLiterals.h"

#include <algorithm>

using namespace Core::Literals;

namespace PhysicsRuntime {

    constinit const Core::FLength PhysicsRuntime::FKSphereElem::mDefaultRadius{0.5_m};

    /**
     * @brief 컴포넌트 스케일을 반영한 구 형상을 반환합니다.
     */
    PhysicsRuntime::FKSphereElem PhysicsRuntime::FKSphereElem::GetScaled(const FVector& Scale) const {
        const FVector AbsoluteScale{Scale.Abs()};
        PhysicsRuntime::FKSphereElem Scaled{*this};
        Scaled.mCenter = mCenter * Scale;
        Scaled.mRadius = mRadius * std::min({AbsoluteScale.x, AbsoluteScale.y, AbsoluteScale.z});

        return Scaled;
    }

}
