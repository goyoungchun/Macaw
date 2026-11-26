/**
 * @file FClothVertBoneData.h
 * @brief 천 입자 하나를 스키닝할 뼈 영향을 선언합니다.
 * @ingroup PhysicsRuntime
 */

#pragma once

#include "Core/Types.h"

namespace PhysicsRuntime {

    /**
     * @brief 천 입자 하나가 받을 수 있는 최대 뼈 영향 수입니다.
     */
    inline constexpr Index_t MaxTotalInfluences{4};

    /**
     * @brief 천 입자 하나를 기준 포즈로 옮길 때 쓰는 뼈 번호와 가중치입니다.
     * @details 뼈 번호는 천 에셋의 사용 뼈 목록 안의 번호이며 메시의 뼈 번호가 아닙니다.
     *          가중치가 큰 순서로 앞에서부터 채웁니다.
     * @ingroup PhysicsRuntime
     */
    struct FClothVertBoneData {
        /**
         * @brief 가중치가 0이 아닌 영향의 수입니다.
         */
        Index_t mNumInfluences{};

        /**
         * @brief 천 에셋의 사용 뼈 목록 안의 번호입니다.
         */
        Core::TStaticArray<Uint32, MaxTotalInfluences> mBoneIndices{};

        /**
         * @brief 뼈 번호 순서에 대응하는 가중치입니다.
         */
        Core::TStaticArray<float, MaxTotalInfluences> mBoneWeights{};
    };
}
