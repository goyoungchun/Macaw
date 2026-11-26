/**
 * @file FClothPhysicalMeshData.h
 * @brief 천 시뮬레이션에 쓰는 입자와 삼각형 데이터를 선언합니다.
 * @ingroup PhysicsRuntime
 */

#pragma once

#include "Core/Types.h"
#include "Core/Literals/QuantityTypes.h"
#include "Core/Math/FVector.h"
#include "Core/Math/FVector4.h"
#include "PhysicsRuntime/FClothVertBoneData.h"
#include "PhysicsRuntime/FPointWeightMap.h"

namespace PhysicsRuntime {

    /**
     * @brief 천 시뮬레이션 메시의 입자, 삼각형, 질량, 뼈 영향, 가중치 맵입니다.
     * @details 입자 위치와 법선은 메시의 바인드 포즈 좌표입니다.
     * @ingroup PhysicsRuntime
     */
    struct FClothPhysicalMeshData {
    public:

        /**
         * @brief 기본 가중치 맵을 가진 빈 메시를 구성합니다.
         */
        FClothPhysicalMeshData();

    public:

        /**
         * @brief 배열을 입자 수와 인덱스 수에 맞춰 0으로 채우고 기본 가중치 맵을 다시 만듭니다.
         * @param[in] NumVerts 입자 수입니다.
         * @param[in] NumIndices 삼각형 인덱스 수입니다.
         */
        void Reset(Index_t NumVerts, Index_t NumIndices);

        /**
         * @brief 모든 가중치 맵을 지우고 기본 대상의 빈 맵을 추가합니다.
         */
        void ClearWeightMaps();

        /**
         * @brief 대상의 가중치 맵이 있는지 검사합니다.
         * @param[in] Target 찾을 매개변수입니다.
         * @return 맵이 있으면 true입니다.
         */
        bool HasWeightMap(EWeightMapTargetCommon Target) const;

        /**
         * @brief 대상의 가중치 맵을 찾습니다.
         * @param[in] Target 찾을 매개변수입니다.
         * @return 맵의 주소이며 없으면 nullptr입니다.
         */
        const FPointWeightMap* FindWeightMap(EWeightMapTargetCommon Target) const;

        /**
         * @brief 대상의 가중치 맵을 찾습니다.
         * @param[in] Target 찾을 매개변수입니다.
         * @return 맵의 주소이며 없으면 nullptr입니다.
         */
        FPointWeightMap* FindWeightMap(EWeightMapTargetCommon Target);

        /**
         * @brief 대상의 가중치 맵을 찾고 없으면 빈 맵을 추가합니다.
         * @param[in] Target 찾거나 추가할 매개변수입니다.
         * @return 맵의 참조입니다.
         */
        FPointWeightMap& AddWeightMap(EWeightMapTargetCommon Target);

        /**
         * @brief 천끼리 부딪힐 입자를 고릅니다.
         * @details 고정되지 않은 입자 중에서 처음부터 서로 거리 안에 있는 입자는 앞의 것만 남깁니다.
         * @param[in] SelfCollisionRadius 고를 때 쓰는 거리(m)이며 0이면 아무것도 바꾸지 않습니다.
         */
        void BuildSelfCollisionData(Core::FLength SelfCollisionRadius);

    public:

        /**
         * @brief 입자의 바인드 포즈 위치입니다.
         */
        Core::TArray<FVector> mVertices{};

        /**
         * @brief 입자의 바인드 포즈 법선입니다.
         */
        Core::TArray<FVector> mNormals{};

        /**
         * @brief 입자의 정점 색입니다.
         */
        Core::TArray<FVector4> mVertexColors{};

        /**
         * @brief 삼각형마다 입자 번호 세 개를 나열한 배열입니다.
         */
        Core::TArray<Uint32> mIndices{};

        /**
         * @brief 매개변수 종류를 키로 한 가중치 맵입니다.
         */
        Core::TMap<Uint32, FPointWeightMap> mWeightMaps{};

        /**
         * @brief 입자의 역질량이며 0이면 시뮬레이션하지 않고 스키닝 위치를 따릅니다.
         */
        Core::TArray<float> mInverseMasses{};

        /**
         * @brief 입자를 기준 포즈로 옮길 때 쓰는 뼈 영향입니다.
         */
        Core::TArray<FClothVertBoneData> mBoneData{};

        /**
         * @brief 입자 하나가 가진 뼈 영향 수의 최댓값입니다.
         */
        Index_t mMaxBoneWeights{};

        /**
         * @brief 역질량이 0인 고정 입자의 수입니다.
         */
        Index_t mNumFixedVerts{};

        /**
         * @brief 천끼리 부딪힐 입자 번호입니다.
         */
        Core::TArray<Uint32> mSelfCollisionIndices{};
    };
}
