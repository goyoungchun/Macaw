/**
 * @file FKBoxElem.h
 * @brief 바디 셋업에 포함되는 상자 충돌 형상입니다.
 * @ingroup PhysicsRuntime
 */

#pragma once

#include "Core/ReflectionMacros.h"
#include "Core/Literals/UnitLiterals.h"
#include "Core/Math/FVector.h"
#include "Core/Math/FRotator.h"
#include "FKBoxElem.generated.h"

namespace PhysicsRuntime {

    /**
     * @brief 바디 셋업에 포함되는 상자 충돌 형상입니다.
     * @details 중심과 회전은 바디 로컬 공간 기준이며 반길이는 컴포넌트 스케일을 적용하기 전 값입니다.
     * @ingroup PhysicsRuntime
     */
    USTRUCT()
    struct FKBoxElem {
        GENERATED_BODY()

    public:

        /**
         * @brief 컴포넌트 스케일을 반영한 상자 형상을 반환합니다.
         * @param[in] Scale 바디에 적용할 컴포넌트 스케일입니다.
         * @return 중심은 축별 스케일을, 절반 크기는 축별 스케일의 절댓값을 따릅니다.
         * @note 물리 형상 생성과 디버그 표시가 같은 규칙을 쓰도록 이 함수로 스케일을 적용합니다.
         */
        PhysicsRuntime::FKBoxElem GetScaled(const FVector& Scale) const;

    public:

        /**
         * @brief 바디 로컬 공간에서의 상자 중심입니다.
         */
        UPROPERTY(Edit, Serialize)
        FVector mCenter{};

        /**
         * @brief 바디 로컬 공간에서의 상자 회전입니다.
         */
        UPROPERTY(Edit, Serialize)
        FRotator mRotation{};

        /**
         * @brief 스케일을 적용하기 전 상자의 축별 반길이(m)입니다.
         */
        UPROPERTY(Edit, Serialize)
        FVector mHalfExtent{mDefaultHalfExtent, mDefaultHalfExtent, mDefaultHalfExtent};

    private:
        static const float mDefaultHalfExtent;
    };

}
