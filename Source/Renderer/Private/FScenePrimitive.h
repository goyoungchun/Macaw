/**
 * @file FScenePrimitive.h
 * @brief 렌더 장면의 프리미티브 프록시와 GPU 객체 상수 버퍼입니다.
 * @ingroup Renderer
 */

#pragma once

#include "Core/ReflectionMacros.h"
#include "Engine/FPrimitiveSceneProxy.h"
#include "FClothConstants.h"
#include "FObjectConstants.h"
#include "RenderCore/TConstantBuffer.h"
#include "RenderCore/TStructuredBuffer.h"
#include "Core/Types.h"

#include <d3d11.h>
#include <wrl/client.h>

namespace Renderer {

    /**
     * @brief 렌더 장면의 프리미티브 프록시와 GPU 객체 상수 버퍼입니다.
     * @ingroup Renderer
     */
    struct FScenePrimitive {
    public:
        /**
         * @brief 한 원소가 스키닝 행렬과 법선용 역전치 행렬인 본 팔레트 버퍼입니다.
         */
        using FSkinBuffer = RenderCore::TStructuredBuffer<Core::TStaticArray<FMatrix, 2>>;

        /**
         * @brief 천 에셋 하나의 입자 위치와 법선 버퍼입니다.
         */
        using FClothParticleBuffer = RenderCore::TStructuredBuffer<Renderer::FClothParticleGpu>;

        /**
         * @brief 천 에셋 하나를 그릴 때 정점 셰이더에 넘기는 GPU 자원입니다.
         */
        struct FClothResources {
            /**
             * @brief 기준 뼈 좌표의 입자 위치와 법선입니다.
             */
            FClothParticleBuffer mParticles{};

            /**
             * @brief 입자 좌표를 컴포넌트 좌표로 바꾸는 행렬과 섞는 비율입니다.
             */
            RenderCore::TConstantBuffer<Renderer::FClothConstants> mConstants{};
        };

        /**
         * @brief 엔진에서 복사한 프리미티브 프록시입니다.
         */
        Engine::FPrimitiveSceneProxy mProxy{};

        /**
         * @brief 객체별 GPU 상수 버퍼입니다.
         */
        RenderCore::TConstantBuffer<Renderer::FObjectConstants> mObjectBuffer{};

        /**
         * @brief 프리미티브 드로에 사용하는 월드·UV·그림자 상수의 CPU 사본입니다.
         */
        Renderer::FObjectConstants mObjectConstants{};

        /**
         * @brief 객체별 스키닝 행렬과 법선용 역전치 행렬을 담아 정점 셰이더에서 읽는 구조화 버퍼입니다.
         */
        FSkinBuffer mSkinBuffer{};

        /**
         * @brief 마지막으로 GPU에 업로드한 포즈 개정입니다.
         */
        Core::FRevision mUploadedPoseRevision{};

        /**
         * @brief 천 에셋 번호별 입자 버퍼와 천 상수입니다.
         */
        Core::TMap<Index_t, FClothResources> mClothResources{};

        /**
         * @brief 마지막으로 GPU에 업로드한 천 결과 개정입니다.
         */
        Core::FRevision mUploadedClothRevision{};
    };

}
