/**
 * @file FKSphylElem.h
 * @brief 바디 셋업에 포함되는 캡슐 충돌 형상입니다.
 * @ingroup PhysicsRuntime
 */

#pragma once

#include "Core/ReflectionMacros.h"
#include "Core/Literals/UnitLiterals.h"
#include "Core/Math/FVector.h"
#include "Core/Math/FRotator.h"
#include "FKSphylElem.generated.h"

namespace PhysicsRuntime {

    /**
     * @brief 바디 셋업에 포함되는 캡슐 충돌 형상입니다.
     * @details 캡슐의 축은 회전을 적용하기 전 로컬 Z축이며, 길이는 양 끝 반구 중심 사이의 거리입니다.
     *          중심과 회전은 바디 로컬 공간 기준이고 크기는 컴포넌트 스케일을 적용하기 전 값입니다.
     * @ingroup PhysicsRuntime
     */
    USTRUCT()
    struct FKSphylElem {
        GENERATED_BODY()

    public:

        /**
         * @brief 컴포넌트 스케일을 반영한 캡슐 형상을 반환합니다.
         * @param[in] Scale 바디에 적용할 컴포넌트 스케일입니다.
         * @return 중심은 축별 스케일을, 반지름은 X·Y 중 큰 스케일, 길이는 Z 스케일의 절댓값을 따릅니다.
         * @note 물리 형상 생성과 디버그 표시가 같은 규칙을 쓰도록 이 함수로 스케일을 적용합니다.
         */
        PhysicsRuntime::FKSphylElem GetScaled(const FVector& Scale) const;

    public:

        /**
         * @brief 바디 로컬 공간에서의 캡슐 중심입니다.
         */
        UPROPERTY(Edit, Serialize)
        FVector mCenter{};

        /**
         * @brief 바디 로컬 공간에서의 캡슐 회전입니다.
         */
        UPROPERTY(Edit, Serialize)
        FRotator mRotation{};

        /**
         * @brief 스케일을 적용하기 전 캡슐의 반지름(m)입니다.
         */
        UPROPERTY(Edit, Serialize)
        Core::FLength mRadius{mDefaultRadius};

        /**
         * @brief 스케일을 적용하기 전 양 끝 반구 중심 사이의 거리(m)입니다.
         */
        UPROPERTY(Edit, Serialize)
        Core::FLength mLength{mDefaultLength};

    private:
        static const Core::FLength mDefaultRadius;
        static const Core::FLength mDefaultLength;
    };

}
