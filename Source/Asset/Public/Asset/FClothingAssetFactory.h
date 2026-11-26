/**
 * @file FClothingAssetFactory.h
 * @brief 스켈레탈 메시의 섹션으로 천 에셋을 만들고 섹션에 연결하는 함수를 선언합니다.
 * @ingroup Asset
 */

#pragma once

#include "Core/Types.h"

#include <memory>

namespace PhysicsRuntime {
    class UClothingAssetCommon;
    class UPhysicsAsset;
    struct FClothLODDataCommon;
}

namespace Asset {
    class USkeletalMesh;
    class USkeleton;

    /**
     * @brief 스켈레탈 메시의 섹션으로 천 에셋을 만들고 섹션에 연결하는 함수 모음입니다.
     * @ingroup Asset
     */
    namespace FClothingAssetFactory {
        /**
         * @brief 섹션 하나를 천 메시로 바꾼 새 천 에셋을 만듭니다.
         * @details 만든 에셋은 메시에 추가하지 않으므로 호출한 쪽이 메시에 넘깁니다.
         *          최대 거리는 모두 0이라서 모든 입자가 고정된 상태로 시작합니다.
         * @param[in] TargetMesh 섹션을 가진 메시입니다.
         * @param[in] Skeleton 메시가 사용하는 스켈레톤입니다.
         * @param[in] SubMeshIndex 천으로 바꿀 섹션 번호입니다.
         * @param[in] PhysicsAsset 천이 부딪힐 몸을 담은 물리 에셋이며 nullptr이면 몸과 부딪히지 않습니다.
         * @return 새 천 에셋이며 섹션이 없거나 이미 천이 연결되었거나 천 메시를 만들 수 없으면 nullptr입니다.
         */
        std::unique_ptr<PhysicsRuntime::UClothingAssetCommon> CreateFromSkeletalMesh(const USkeletalMesh& TargetMesh, const USkeleton& Skeleton, Index_t SubMeshIndex, PhysicsRuntime::UPhysicsAsset* PhysicsAsset);

        /**
         * @brief 섹션의 정점을 위치로 합쳐 천 상세 단계 하나를 채웁니다.
         * @param[in] SourceMesh 섹션을 가진 메시입니다.
         * @param[in] Skeleton 메시가 사용하는 스켈레톤입니다.
         * @param[in] SubMeshIndex 천으로 바꿀 섹션 번호입니다.
         * @param[in,out] DestAsset 사용 뼈 이름을 추가할 천 에셋입니다.
         * @param[out] DestLod 채울 상세 단계입니다.
         * @return 천 메시를 만들었으면 true입니다.
         */
        bool ImportToLodInternal(const USkeletalMesh& SourceMesh, const USkeleton& Skeleton, Index_t SubMeshIndex, PhysicsRuntime::UClothingAssetCommon& DestAsset, PhysicsRuntime::FClothLODDataCommon& DestLod);

        /**
         * @brief 섹션이 천 에셋의 상세 단계를 따르도록 정점 대응을 만들어 연결합니다.
         * @param[in,out] SkelMesh 천 에셋을 소유하고 섹션 연결 데이터를 저장할 메시입니다.
         * @param[in,out] ClothingAsset 메시가 소유한 천 에셋입니다.
         * @param[in] SubMeshIndex 연결할 섹션 번호입니다.
         * @param[in] AssetLodIndex 연결할 천 상세 단계 번호입니다.
         * @return 연결했으면 true입니다.
         */
        bool BindToSkeletalMesh(USkeletalMesh& SkelMesh, PhysicsRuntime::UClothingAssetCommon& ClothingAsset, Index_t SubMeshIndex, Index_t AssetLodIndex);
    }
}
