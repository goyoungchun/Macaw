/**
 * @file FClothingSectionData.h
 * @brief 스켈레탈 메시의 섹션 하나가 천 에셋을 따르기 위한 연결 데이터를 선언합니다.
 * @ingroup Asset
 */

#pragma once

#include "Core/Guid.h"
#include "Core/Types.h"
#include "PhysicsRuntime/FMeshToMeshVertData.h"

namespace Core {
    class FArchive;
}

namespace Asset {

    /**
     * @brief 섹션 하나가 어떤 천 에셋의 어느 상세 단계를 따르는지와 정점 대응입니다.
     * @ingroup Asset
     */
    struct FClothingSectionData {
        /**
         * @brief 천 에셋과 상세 단계가 지정되어 있는지 검사합니다.
         * @return 연결되어 있으면 true입니다.
         */
        bool IsValid() const;

        /**
         * @brief 연결 데이터를 저장하거나 불러옵니다.
         * @param[in,out] Archive 저장 또는 불러오기 방향을 가진 아카이브입니다.
         */
        void Serialize(Core::FArchive& Archive);

        /**
         * @brief 섹션이 따르는 천 에셋의 식별자입니다.
         */
        Core::FGuid mAssetGuid{Core::FGuid::Null()};

        /**
         * @brief 천 에셋 안에서 쓰는 상세 단계 번호입니다.
         */
        Index_t mAssetLodIndex{InvalidIndex};

        /**
         * @brief 스켈레탈 메시의 천 에셋 목록에서 그 에셋의 번호입니다.
         */
        Index_t mCorrespondClothAssetIndex{InvalidIndex};

        /**
         * @brief 섹션 정점마다 천 메시 삼각형 위의 위치입니다.
         */
        Core::TArray<PhysicsRuntime::FMeshToMeshVertData> mClothMappingData{};

        /**
         * @brief 정점 대응 순서대로의 메시 정점 번호입니다.
         */
        Core::TArray<Uint32> mSectionVertexIndices{};
    };
}
