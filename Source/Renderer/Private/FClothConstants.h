/**
 * @file FClothConstants.h
 * @brief 천 섹션을 그릴 때 정점 셰이더에 넘기는 상수와 입자 배치를 선언합니다.
 * @ingroup Renderer
 */

#pragma once

#include "Core/Types.h"
#include "Core/Math/FMatrix.h"

#include <type_traits>

namespace Renderer {

    /**
     * @brief 천 섹션을 그릴 때 정점 셰이더에 넘기는 상수이며 Cloth.hlsl의 ClothConstants와 같아야 합니다.
     * @ingroup Renderer
     */
    struct FClothConstants {
    public:
        /**
         * @brief 입자 좌표(기준 뼈 좌표)를 컴포넌트 좌표로 바꾸는 행렬입니다.
         */
        FMatrix mClothLocalToComponent{};

        /**
         * @brief 스키닝 위치와 시뮬레이션 위치를 섞는 비율입니다.
         */
        float mClothBlendWeight{};

        /**
         * @brief 16바이트 정렬을 맞추는 빈 칸입니다.
         */
        Core::TStaticArray<float, 3> mPadding{};
    };

    /**
     * @brief 천 입자 하나의 위치와 법선이며 Cloth.hlsl의 FClothParticle과 같아야 합니다.
     * @ingroup Renderer
     */
    struct FClothParticleGpu {
    public:
        /**
         * @brief 기준 뼈 좌표의 입자 위치입니다.
         */
        Core::TStaticArray<float, 3> mPosition{};

        /**
         * @brief 기준 뼈 좌표의 입자 법선입니다.
         */
        Core::TStaticArray<float, 3> mNormal{};
    };

    static_assert(std::is_standard_layout_v<Renderer::FClothConstants> && sizeof(Renderer::FClothConstants) == 80);
    static_assert(sizeof(Renderer::FClothParticleGpu) == 24);

}
