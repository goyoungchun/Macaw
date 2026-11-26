/**
 * @file FClothPhysicalMeshData.cpp
 * @brief 천 시뮬레이션에 쓰는 입자와 삼각형 데이터를 구현합니다.
 * @ingroup PhysicsRuntime
 */

#include "PhysicsRuntime/FClothPhysicalMeshData.h"

namespace PhysicsRuntime {

    /**
     * @brief 기본 가중치 맵을 가진 빈 메시를 구성합니다.
     */
    FClothPhysicalMeshData::FClothPhysicalMeshData() {
        ClearWeightMaps();
    }

    /**
     * @brief 배열을 입자 수와 인덱스 수에 맞춰 0으로 채우고 기본 가중치 맵을 다시 만듭니다.
     */
    void FClothPhysicalMeshData::Reset(Index_t NumVerts, Index_t NumIndices) {
        mVertices.assign(NumVerts, FVector{0.0f, 0.0f, 0.0f});
        mNormals.assign(NumVerts, FVector{0.0f, 0.0f, 0.0f});
        mVertexColors.assign(NumVerts, FVector4{0.0f, 0.0f, 0.0f, 1.0f});
        mInverseMasses.assign(NumVerts, 0.0f);
        mBoneData.assign(NumVerts, FClothVertBoneData{});
        mIndices.assign(NumIndices, 0);

        mNumFixedVerts = 0;
        mMaxBoneWeights = 0;

        ClearWeightMaps();
    }

    /**
     * @brief 모든 가중치 맵을 지우고 기본 대상의 빈 맵을 추가합니다.
     */
    void FClothPhysicalMeshData::ClearWeightMaps() {
        mWeightMaps.clear();

        // 시뮬레이션 코드가 항상 찾을 수 있도록 기본 대상의 빈 맵을 둡니다.
        AddWeightMap(EWeightMapTargetCommon::MaxDistance);
        AddWeightMap(EWeightMapTargetCommon::BackstopDistance);
        AddWeightMap(EWeightMapTargetCommon::BackstopRadius);
        AddWeightMap(EWeightMapTargetCommon::AnimDriveMultiplier);
    }

    /**
     * @brief 대상의 가중치 맵이 있는지 검사합니다.
     */
    bool FClothPhysicalMeshData::HasWeightMap(EWeightMapTargetCommon Target) const {
        return mWeightMaps.contains(static_cast<Uint32>(Target));
    }

    /**
     * @brief 대상의 가중치 맵을 찾습니다.
     */
    const FPointWeightMap* FClothPhysicalMeshData::FindWeightMap(EWeightMapTargetCommon Target) const {
        const auto Found{mWeightMaps.find(static_cast<Uint32>(Target))};

        return Found != mWeightMaps.end() ? &Found->second : nullptr;
    }

    /**
     * @brief 대상의 가중치 맵을 찾습니다.
     */
    FPointWeightMap* FClothPhysicalMeshData::FindWeightMap(EWeightMapTargetCommon Target) {
        const auto Found{mWeightMaps.find(static_cast<Uint32>(Target))};

        return Found != mWeightMaps.end() ? &Found->second : nullptr;
    }

    /**
     * @brief 대상의 가중치 맵을 찾고 없으면 빈 맵을 추가합니다.
     */
    FPointWeightMap& FClothPhysicalMeshData::AddWeightMap(EWeightMapTargetCommon Target) {
        return mWeightMaps[static_cast<Uint32>(Target)];
    }

    /**
     * @brief 천끼리 부딪힐 입자를 고릅니다.
     */
    void FClothPhysicalMeshData::BuildSelfCollisionData(Core::FLength SelfCollisionRadius) {
        if (SelfCollisionRadius == Core::FLength{}) {
            return;
        }

        const Core::FArea RadiusSquared{SelfCollisionRadius * SelfCollisionRadius};
        const FPointWeightMap* const MaxDistances{FindWeightMap(EWeightMapTargetCommon::MaxDistance)};
        constexpr Uint32 RemovedIndex{0xFFFFFFFFu};

        // 고정 입자는 움직이지 않으므로 천끼리 부딪힐 필요가 없습니다.
        mSelfCollisionIndices.clear();

        for (Index_t VertIndex{}; VertIndex < mVertices.size(); ++VertIndex) {
            if (MaxDistances == nullptr || !MaxDistances->IsBelowThreshold(VertIndex)) {
                mSelfCollisionIndices.push_back(static_cast<Uint32>(VertIndex));
            }
        }

        // 처음부터 거리 안에 붙어 있는 입자끼리는 늘 부딪힌 것으로 보이므로 뒤의 입자를 뺍니다.
        for (Index_t Vert0Itr{}; Vert0Itr < mSelfCollisionIndices.size(); ++Vert0Itr) {
            const Uint32 V0Index{mSelfCollisionIndices[Vert0Itr]};

            if (V0Index == RemovedIndex) {
                continue;
            }

            const FVector& V0Pos{mVertices[V0Index]};

            for (Index_t Vert1Itr{Vert0Itr + 1}; Vert1Itr < mSelfCollisionIndices.size(); ++Vert1Itr) {
                const Uint32 V1Index{mSelfCollisionIndices[Vert1Itr]};

                if (V1Index != RemovedIndex && Core::FArea::FromBase((mVertices[V1Index] - V0Pos).LengthSquared()) < RadiusSquared) {
                    mSelfCollisionIndices[Vert1Itr] = RemovedIndex;
                }
            }
        }

        std::erase(mSelfCollisionIndices, RemovedIndex);
    }
}
