/**
 * @file FClothLODDataCommon.h
 * @brief 천 에셋의 상세 단계 하나에 쓰는 데이터를 선언합니다.
 * @ingroup PhysicsRuntime
 */

#pragma once

#include "Core/Types.h"
#include "PhysicsRuntime/FClothCollisionData.h"
#include "PhysicsRuntime/FClothPhysicalMeshData.h"
#include "PhysicsRuntime/FMeshToMeshVertData.h"
#include "PhysicsRuntime/FPointWeightMap.h"

namespace PhysicsRuntime {

    /**
     * @brief 천 에셋의 상세 단계 하나에 쓰는 시뮬레이션 메시와 편집용 가중치 맵입니다.
     * @ingroup PhysicsRuntime
     */
    struct FClothLODDataCommon {
        /**
         * @brief 시뮬레이션에 쓰는 입자와 삼각형입니다.
         */
        FClothPhysicalMeshData mPhysicalMeshData{};

        /**
         * @brief 편집기에서 칠하는 가중치 맵이며 적용하면 시뮬레이션 메시의 맵으로 복사합니다.
         */
        Core::TArray<FPointWeightMap> mPointWeightMaps{};

        /**
         * @brief 더 자세한 단계에서 이 단계로 바뀔 때 입자 위치를 옮기는 데이터입니다.
         */
        Core::TArray<FMeshToMeshVertData> mTransitionUpSkinData{};

        /**
         * @brief 덜 자세한 단계에서 이 단계로 바뀔 때 입자 위치를 옮기는 데이터입니다.
         */
        Core::TArray<FMeshToMeshVertData> mTransitionDownSkinData{};

        /**
         * @brief 이 단계에 직접 넣어 둔 충돌체이며, 물리 에셋에서 가져오는 충돌체와 함께 씁니다.
         */
        FClothCollisionData mCollisionData{};
    };
}
