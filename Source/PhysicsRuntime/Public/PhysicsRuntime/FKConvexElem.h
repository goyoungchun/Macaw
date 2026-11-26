/**
 * @file FKConvexElem.h
 * @brief 바디 셋업에 포함되는 볼록 다면체 충돌 형상입니다.
 * @ingroup PhysicsRuntime
 */

#pragma once

#include "Core/ReflectionMacros.h"
#include "Core/Types.h"
#include "Core/Math/FVector.h"
#include "FKConvexElem.generated.h"

namespace PhysicsRuntime {

    /**
     * @brief 바디 셋업에 포함되는 볼록 다면체 충돌 형상입니다.
     * @details 정점은 바디 로컬 공간의 볼록 외곽 꼭짓점이며 컴포넌트 스케일을 적용하기 전 값입니다.
     *          PhysX 볼록 메시는 저장하지 않고 바디를 만들 때 정점으로 쿠킹합니다.
     *          삼각형 인덱스는 외곽 면을 삼각형으로 나눈 목록이며 디버그 표시에 사용합니다.
     * @ingroup PhysicsRuntime
     */
    USTRUCT()
    struct FKConvexElem {
        GENERATED_BODY()

    public:

        /**
         * @brief 컴포넌트 스케일을 반영한 볼록 형상을 반환합니다.
         * @param[in] Scale 바디에 적용할 컴포넌트 스케일입니다.
         * @return 정점마다 축별 스케일을 곱한 형상입니다.
         * @note 물리 형상은 PhysX 메시 스케일로 같은 규칙을 적용하므로, 이 함수는 디버그 표시에 사용합니다.
         */
        PhysicsRuntime::FKConvexElem GetScaled(const FVector& Scale) const;

        /**
         * @brief 볼록 다면체를 만들 수 있는 정점과 인덱스를 가졌는지 검사합니다.
         * @return 정점이 4개 이상이고 인덱스가 정점 범위 안의 삼각형 목록이면 true입니다.
         */
        bool IsValid() const;

    public:

        /**
         * @brief 바디 로컬 공간의 볼록 외곽 꼭짓점입니다.
         */
        UPROPERTY(Serialize)
        Core::TArray<FVector> mVertices{};

        /**
         * @brief 외곽 면을 나눈 삼각형의 정점 인덱스 목록이며 세 개씩 삼각형 하나를 이룹니다.
         */
        UPROPERTY(Serialize)
        Core::TArray<Uint32> mIndices{};
    };

}
