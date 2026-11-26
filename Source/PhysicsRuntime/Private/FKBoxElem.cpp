/**
 * @file FKBoxElem.cpp
 * @brief 바디 셋업에 포함되는 상자 충돌 형상입니다.
 * @ingroup PhysicsRuntime
 */

#include "Core/Types.h"
#include "PhysicsRuntime/FKBoxElem.h"
#include "Core/Literals/UnitLiterals.h"

using namespace Core::Literals;

namespace PhysicsRuntime {

    constinit const float PhysicsRuntime::FKBoxElem::mDefaultHalfExtent{(0.5_m).GetBase()};

    /**
     * @brief 컴포넌트 스케일을 반영한 상자 형상을 반환합니다.
     */
    PhysicsRuntime::FKBoxElem PhysicsRuntime::FKBoxElem::GetScaled(const FVector& Scale) const {
        PhysicsRuntime::FKBoxElem Scaled{*this};
        Scaled.mCenter = mCenter * Scale;
        Scaled.mHalfExtent = mHalfExtent * Scale.Abs();

        return Scaled;
    }

}
