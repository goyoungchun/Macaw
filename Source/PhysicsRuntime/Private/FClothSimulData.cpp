/**
 * @file FClothSimulData.cpp
 * @brief 천 하나의 시뮬레이션 결과를 구현합니다.
 * @ingroup PhysicsRuntime
 */

#include "PhysicsRuntime/FClothSimulData.h"

namespace PhysicsRuntime {

    /**
     * @brief 결과를 비웁니다.
     */
    void FClothSimulData::Reset() {
        mPositions.clear();
        mNormals.clear();
        mTransform = FTransform{};
        mComponentRelativeTransform = FTransform{};
    }
}
