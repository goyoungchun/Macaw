/**
 * @file FObjectConstants.h
 * @brief 객체별 월드 행렬, 역행렬과 UV 변환을 전달하는 GPU 상수 버퍼 레이아웃입니다.
 * @ingroup Renderer
 */

#pragma once

#include "Core/ReflectionMacros.h"
#include "Core/Math/FMatrix.h"
#include "Core/Math/FVector4.h"

#include <cstddef>
#include <type_traits>

namespace Renderer {

    /**
     * @brief 객체별 월드 행렬, 역행렬과 UV 변환을 전달하는 GPU 상수 버퍼 레이아웃입니다.
     * @ingroup Renderer
     */
    struct FObjectConstants {
    public:
        /**
         * @brief 오브젝트의 월드 변환 행렬입니다.
         */
        FMatrix mWorld{};

        /**
         * @brief 월드 변환의 역행렬입니다.
         */
        FMatrix mWorldInverse{};

        /**
         * @brief XY 배율과 ZW 오프셋으로 구성된 UV 변환입니다.
         */
        FVector4 mUVTransform{1.0f, 1.0f, 0.0f, 0.0f};
    };

    struct FMeshInstanceData {
    public:
        Renderer::FObjectConstants mObject{};
        FVector4 mParticleColor{1.0f, 1.0f, 1.0f, 1.0f};
    };

    static_assert(sizeof(FMatrix) == 64 && alignof(FMatrix) == 16 && std::is_trivially_copyable_v<FMatrix>);
    static_assert(std::is_standard_layout_v<Renderer::FObjectConstants> && sizeof(Renderer::FObjectConstants) == 144);
    static_assert(offsetof(Renderer::FObjectConstants, mWorld) == 0 && offsetof(Renderer::FObjectConstants, mWorldInverse) == 64);
    static_assert(std::is_standard_layout_v<Renderer::FMeshInstanceData> && offsetof(Renderer::FMeshInstanceData, mObject) == 0);

}
