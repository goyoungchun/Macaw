/**
 * @file FClothSimulData.h
 * @brief 천 하나의 시뮬레이션 결과를 선언합니다.
 * @ingroup PhysicsRuntime
 */

#pragma once

#include "Core/Types.h"
#include "Core/Math/FTransform.h"
#include "Core/Math/FVector.h"

namespace PhysicsRuntime {

    /**
     * @brief 천 하나의 입자 위치, 법선과 그 좌표의 기준 변환입니다.
     * @details 위치와 법선은 기준 뼈 좌표이며, 기준 변환을 곱하면 월드 좌표가 됩니다.
     * @ingroup PhysicsRuntime
     */
    struct FClothSimulData {
        /**
         * @brief 결과를 비웁니다.
         */
        void Reset();

        /**
         * @brief 기준 뼈 좌표의 입자 위치입니다.
         */
        Core::TArray<FVector> mPositions{};

        /**
         * @brief 기준 뼈 좌표의 입자 법선입니다.
         */
        Core::TArray<FVector> mNormals{};

        /**
         * @brief 기준 뼈 좌표를 월드 좌표로 바꾸는 변환입니다.
         */
        FTransform mTransform{};

        /**
         * @brief 기준 뼈 좌표를 컴포넌트 좌표로 바꾸는 변환입니다.
         */
        FTransform mComponentRelativeTransform{};
    };
}
