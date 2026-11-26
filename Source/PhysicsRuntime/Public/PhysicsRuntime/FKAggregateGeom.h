/**
 * @file FKAggregateGeom.h
 * @brief 하나의 바디를 구성하는 단순 충돌 형상 묶음입니다.
 * @ingroup PhysicsRuntime
 */

#pragma once

#include "Core/Math/FBox.h"
#include "Core/Types.h"
#include "Core/ReflectionMacros.h"
#include "PhysicsRuntime/FKSphereElem.h"
#include "PhysicsRuntime/FKBoxElem.h"
#include "PhysicsRuntime/FKSphylElem.h"
#include "PhysicsRuntime/FKConvexElem.h"

#include <optional>

#include "FKAggregateGeom.generated.h"

namespace PhysicsRuntime {

    /**
     * @brief 하나의 바디를 구성하는 단순 충돌 형상 묶음입니다.
     * @details 모든 형상은 같은 바디 로컬 공간을 공유하며, 바디를 만들 때 각각 하나의 PhysX 형상이 됩니다.
     * @ingroup PhysicsRuntime
     */
    USTRUCT()
    struct FKAggregateGeom {
        GENERATED_BODY()

    public:

        /**
         * @brief 포함한 모든 형상의 개수를 반환합니다.
         * @return 구·상자·캡슐·볼록 형상 개수의 합입니다.
         */
        Index_t GetElementCount() const;

        /**
         * @brief 포함한 형상이 없는지 검사합니다.
         * @return 형상이 하나도 없으면 true입니다.
         */
        bool IsEmpty() const;

        std::optional<FBox> CalcAABB(const FMatrix& LocalToWorld) const;

    public:

        /**
         * @brief 구 충돌 형상 목록입니다.
         */
        UPROPERTY(Serialize)
        Core::TArray<PhysicsRuntime::FKSphereElem> mSphereElems{};

        /**
         * @brief 상자 충돌 형상 목록입니다.
         */
        UPROPERTY(Serialize)
        Core::TArray<PhysicsRuntime::FKBoxElem> mBoxElems{};

        /**
         * @brief 캡슐 충돌 형상 목록입니다.
         */
        UPROPERTY(Serialize)
        Core::TArray<PhysicsRuntime::FKSphylElem> mSphylElems{};

        /**
         * @brief 볼록 다면체 충돌 형상 목록입니다.
         */
        UPROPERTY(Serialize)
        Core::TArray<PhysicsRuntime::FKConvexElem> mConvexElems{};
    };

}
