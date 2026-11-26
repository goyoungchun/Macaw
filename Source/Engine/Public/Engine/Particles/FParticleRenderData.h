/**
 * @file FParticleRenderData.h
 * @brief 파티클 컴포넌트가 매 Tick 렌더러에 넘기는 에미터별 그리기 데이터입니다.
 * @ingroup Engine
 */

#pragma once

#include "Core/Types.h"
#include "Core/Math/FBox.h"
#include "Core/Math/FMatrix.h"
#include "Core/Math/FVector.h"
#include "Core/Math/FVector4.h"
#include "CoreUObject/TObjectRef.h"
#include "Engine/Particles/Module/UParticleModuleRequired.h"

namespace Asset {
    class UMaterial;
    class UStaticMesh;
}

namespace Engine {

    /**
     * @brief 스프라이트 파티클 하나의 GPU 인스턴스입니다. 위치와 크기는 월드 공간(m)입니다.
     * @details ParticleSprite.hlsl의 FParticleSpriteInstance와 배치가 같아야 합니다.
     */
    struct FParticleSpriteInstance {
    public:
        float mPosition[3]{};

        /**
         * @brief 화면 평면 안의 회전입니다(rad).
         */
        float mRotation{};

        float mSize[2]{};

        /**
         * @brief 정수부는 SubUV 칸 번호, 소수부는 다음 칸과 섞는 비율입니다.
         */
        float mSubImageIndex{};

        /**
         * @brief 생성 순번이며 나이순 정렬에 씁니다.
         */
        Uint32 mCounter{};

        float mColor[4]{};
    };

    static_assert(sizeof(Engine::FParticleSpriteInstance) == 48, "FParticleSpriteInstance must match ParticleSprite.hlsl");

    /**
     * @brief 에미터 하나의 스프라이트 묶음입니다. 정렬은 뷰마다 렌더러가 합니다.
     */
    struct FParticleSpriteBatch {
    public:
        Core::TArray<Engine::FParticleSpriteInstance> mInstances{};

        /**
         * @brief 기본 색 텍스처와 색조를 쓰며 비어 있으면 기본 원형 텍스처를 씁니다.
         */
        CoreUObject::TObjectRef<Asset::UMaterial> mMaterial{};

        Engine::EParticleSortMode mSortMode{Engine::EParticleSortMode::None};

        Uint32 mSubImagesHorizontal{1};

        Uint32 mSubImagesVertical{1};
    };

    struct FParticleMeshInstance {
    public:
        FMatrix mWorld{};
        FMatrix mWorldInverse{};
        FBox mBounds{};
        FVector4 mColor{1.0f, 1.0f, 1.0f, 1.0f};
    };

    /**
     * @brief 에미터 하나의 메시 파티클 인스턴스 묶음입니다.
     */
    struct FParticleMeshBatch {
    public:
        Core::TArray<Engine::FParticleMeshInstance> mInstances{};

        CoreUObject::TObjectRef<Asset::UStaticMesh> mMesh{};

        /**
         * @brief 메시 머티리얼 슬롯마다 쓸 머티리얼이며 비어 있는 슬롯은 기본 머티리얼로 그립니다.
         */
        Core::TArray<CoreUObject::TObjectRef<Asset::UMaterial>> mMaterials{};
    };

    /**
     * @brief 빔·리본 띠의 한 점입니다. 위치는 월드 공간(m)입니다.
     */
    struct FParticleTrailPoint {
    public:
        FVector mPosition{};

        FVector4 mColor{1.0f, 1.0f, 1.0f, 1.0f};

        /**
         * @brief 띠 폭의 절반(m)입니다.
         */
        float mHalfWidth{};

        /**
         * @brief 띠 길이 방향 텍스처 좌표입니다.
         */
        float mU{};
    };

    /**
     * @brief 에미터 하나의 띠 묶음입니다. 띠 방향은 뷰마다 렌더러가 카메라를 향하게 만듭니다.
     */
    struct FParticleTrailBatch {
    public:
        /**
         * @brief 모든 띠의 점을 이어 붙인 목록입니다.
         */
        Core::TArray<Engine::FParticleTrailPoint> mPoints{};

        /**
         * @brief 띠마다 mPoints에서 끝나는 위치(포함하지 않음)입니다.
         */
        Core::TArray<Uint32> mStripEnds{};

        CoreUObject::TObjectRef<Asset::UMaterial> mMaterial{};

        Engine::EParticleSortMode mSortMode{Engine::EParticleSortMode::None};

        /**
         * @brief 띠 하나를 축 둘레로 나눠 겹쳐 그릴 판 수입니다.
         */
        Uint32 mSheets{1};
    };

    /**
     * @brief 컴포넌트 하나의 그리기 데이터입니다. 종류마다 배열 순서가 에미터 그리기 순서입니다.
     */
    struct FParticleRenderData {
    public:
        Core::TArray<Engine::FParticleSpriteBatch> mSprites{};

        Core::TArray<Engine::FParticleMeshBatch> mMeshes{};

        Core::TArray<Engine::FParticleTrailBatch> mTrails{};
    };

}
