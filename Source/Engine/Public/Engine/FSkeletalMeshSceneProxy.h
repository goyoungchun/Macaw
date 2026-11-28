#pragma once

#include "Core/FRevision.h"
#include "Engine/FPrimitiveSceneProxy.h"
#include "PhysicsRuntime/FClothSimulData.h"

namespace Asset {
    class USkeletalMesh;
}

namespace Engine {
    class FSkeletalMeshSceneProxy final : public Engine::FPrimitiveSceneProxy {
    public:
        FSkeletalMeshSceneProxy(CoreUObject::TObjectRef<Asset::USkeletalMesh> SkeletalMesh, Core::TArray<FMatrix> SkinMatrices, Core::FRevision PoseRevision);

    public:
        static const Engine::FSkeletalMeshSceneProxy* FindSkinnedProxy(const Engine::FPrimitiveSceneProxy& Proxy);
        Engine::EVertexFactoryType GetVertexFactoryType() const override;

        Asset::USkeletalMesh* GetSkeletalMesh() const;
        const Core::TArray<FMatrix>& GetSkinMatrices() const;
        Core::FRevision GetPoseRevision() const;

        void SetClothData(const Core::TMap<Index_t, PhysicsRuntime::FClothSimulData>& SimData, float BlendWeight, Core::FRevision Revision);
        const Core::TMap<Index_t, PhysicsRuntime::FClothSimulData>& GetClothSimData() const;
        float GetClothBlendWeight() const;
        Core::FRevision GetClothRevision() const;

    private:
        /**
         * @brief 스키닝할 공유 에셋이며 mMesh는 이 에셋의 바인드 형상을 가리킵니다.
         */
        CoreUObject::TObjectRef<Asset::USkeletalMesh> mSkeletalMesh{};

        /**
         * @brief 이 객체의 바인드 위치를 현재 메시 공간 위치로 옮기는 본 행렬입니다.
         */
        Core::TArray<FMatrix> mSkinMatrices{};

        /**
         * @brief 월드 행렬이 같아도 포즈 업로드와 그림자 무효화를 구분하는 개정입니다.
         */
        Core::FRevision mPoseRevision{};

        /**
         * @brief 천 에셋 번호별 마지막 천 시뮬레이션 결과이며, 천이 없으면 비어 있습니다.
         */
        Core::TMap<Index_t, PhysicsRuntime::FClothSimulData> mClothSimData{};

        /**
         * @brief 천 섹션 정점을 스키닝 위치와 시뮬레이션 위치 사이에서 섞는 비율이며 1이면 시뮬레이션 위치만 씁니다.
         */
        float mClothBlendWeight{};

        /**
         * @brief 천 결과가 같으면 입자 버퍼 업로드를 건너뛰도록 구분하는 개정입니다.
         */
        Core::FRevision mClothRevision{};
    };
}
