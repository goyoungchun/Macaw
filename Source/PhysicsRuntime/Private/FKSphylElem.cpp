/**
 * @file FKSphylElem.cpp
 * @brief 바디 셋업에 포함되는 캡슐 충돌 형상입니다.
 * @ingroup PhysicsRuntime
 */

#include "Core/Types.h"
#include "PhysicsRuntime/FKSphylElem.h"
#include "Core/Literals/UnitLiterals.h"

#include <algorithm>

using namespace Core::Literals;

namespace PhysicsRuntime {

    constinit const Core::FLength PhysicsRuntime::FKSphylElem::mDefaultRadius{0.5_m};
    constinit const Core::FLength PhysicsRuntime::FKSphylElem::mDefaultLength{1_m};

    /**
     * @brief 컴포넌트 스케일을 반영한 캡슐 형상을 반환합니다.
     */
    PhysicsRuntime::FKSphylElem PhysicsRuntime::FKSphylElem::GetScaled(const FVector& Scale) const {
        const FVector AbsoluteScale{Scale.Abs()};
        PhysicsRuntime::FKSphylElem Scaled{*this};
        Scaled.mCenter = mCenter * Scale;
        Scaled.mRadius = mRadius * std::max(AbsoluteScale.x, AbsoluteScale.y);
        Scaled.mLength = mLength * AbsoluteScale.z;

        return Scaled;
    }

}
