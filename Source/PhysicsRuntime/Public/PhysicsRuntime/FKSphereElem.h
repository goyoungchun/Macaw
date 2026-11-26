/**
 * @file FKSphereElem.h
 * @brief 바디 셋업에 포함되는 구 충돌 형상입니다.
 * @ingroup PhysicsRuntime
 */

#pragma once

#include "Core/ReflectionMacros.h"
#include "Core/Literals/UnitLiterals.h"
#include "Core/Math/FVector.h"
#include "FKSphereElem.generated.h"

namespace PhysicsRuntime {

    /**
     * @brief 바디 셋업에 포함되는 구 충돌 형상입니다.
     * @details 중심은 바디 로컬 공간 기준이며 반지름은 컴포넌트 스케일을 적용하기 전 값입니다.
     * @ingroup PhysicsRuntime
     */
    USTRUCT()
    struct FKSphereElem {
        GENERATED_BODY()

    public:

        /**
         * @brief 컴포넌트 스케일을 반영한 구 형상을 반환합니다.
         * @param[in] Scale 바디에 적용할 컴포넌트 스케일입니다.
         * @return 중심은 축별 스케일을, 반지름은 가장 작은 축 스케일의 절댓값을 따릅니다.
         * @note 물리 형상 생성과 디버그 표시가 같은 규칙을 쓰도록 이 함수로 스케일을 적용합니다.
         */
        PhysicsRuntime::FKSphereElem GetScaled(const FVector& Scale) const;

    public:

        /**
         * @brief 바디 로컬 공간에서의 구 중심입니다.
         */
        UPROPERTY(Edit, Serialize)
        FVector mCenter{};

        /**
         * @brief 스케일을 적용하기 전 구의 반지름(m)입니다.
         */
        UPROPERTY(Edit, Serialize)
        Core::FLength mRadius{mDefaultRadius};

    private:
        static const Core::FLength mDefaultRadius;
    };

}
