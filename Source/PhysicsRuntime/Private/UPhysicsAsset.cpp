/**
 * @file UPhysicsAsset.cpp
 * @brief 물리 에셋에서 뼈 이름과 관절 이름으로 설정을 찾습니다.
 * @ingroup PhysicsRuntime
 */

#include "PhysicsRuntime/UPhysicsAsset.h"

#include <algorithm>

namespace PhysicsRuntime {
    /**
     * @brief 바디와 관절이 없는 빈 에셋을 구성합니다.
     */
    UPhysicsAsset::UPhysicsAsset() = default;

    /**
     * @brief 소유한 설계도와 관절 설정을 정리합니다.
     */
    UPhysicsAsset::~UPhysicsAsset() = default;

    /**
     * @brief 뼈 이름으로 바디 설계도의 번호를 찾습니다.
     */
    Index_t UPhysicsAsset::FindBodyIndex(const Core::FString& BodyName) const {
        const auto Found{mBodySetupIndexMap.find(BodyName)};

        return Found != mBodySetupIndexMap.end() ? Found->second : InvalidIndex;
    }

    /**
     * @brief 관절 이름으로 관절 설정의 번호를 찾습니다.
     */
    Index_t UPhysicsAsset::FindConstraintIndex(const Core::FString& ConstraintName) const {
        for (Index_t Index{}; Index < mConstraintSetup.size(); ++Index) {
            if (mConstraintSetup[Index] != nullptr && mConstraintSetup[Index]->mDefaultInstance.mJointName == ConstraintName) {
                return Index;
            }
        }

        return InvalidIndex;
    }

    /**
     * @brief 두 뼈의 바디 사이 충돌이 비활성으로 지정되어 있는지 반환합니다.
     */
    bool UPhysicsAsset::IsCollisionDisabled(const Core::FString& BoneA, const Core::FString& BoneB) const {
        for (const auto& Pair : mCollisionDisabledPairs) {
            if ((Pair.first == BoneA && Pair.second == BoneB) || (Pair.first == BoneB && Pair.second == BoneA)) {
                return true;
            }
        }

        return false;
    }

    /**
     * @brief 두 뼈의 바디 사이 충돌을 비활성으로 지정하거나 해제합니다.
     */
    bool UPhysicsAsset::SetCollisionDisabled(const Core::FString& BoneA, const Core::FString& BoneB, bool Disabled) {
        if (BoneA == BoneB) {
            return false;
        }

        if (Disabled) {
            if (IsCollisionDisabled(BoneA, BoneB)) {
                return false;
            }

            mCollisionDisabledPairs.emplace_back(BoneA, BoneB);

            return true;
        }

        const Index_t Before{mCollisionDisabledPairs.size()};

        std::erase_if(mCollisionDisabledPairs, [&](const std::pair<Core::FString, Core::FString>& Pair) {
            return (Pair.first == BoneA && Pair.second == BoneB) || (Pair.first == BoneB && Pair.second == BoneA);
        });

        return mCollisionDisabledPairs.size() != Before;
    }

    /**
     * @brief 충돌이 비활성인 쌍을 바디 번호의 쌍으로 바꿔 반환합니다.
     */
    Core::TArray<std::pair<Uint32, Uint32>> UPhysicsAsset::BuildCollisionDisabledBodyPairs() const {
        Core::TArray<std::pair<Uint32, Uint32>> Result{};

        for (const auto& Pair : mCollisionDisabledPairs) {
            const Index_t A{FindBodyIndex(Pair.first)};
            const Index_t B{FindBodyIndex(Pair.second)};

            if (A != InvalidIndex && B != InvalidIndex && A != B) {
                Result.emplace_back(static_cast<Uint32>(std::min(A, B)), static_cast<Uint32>(std::max(A, B)));
            }
        }

        return Result;
    }

    /**
     * @brief 뼈 이름에서 설계도 번호를 찾는 표를 다시 만듭니다.
     */
    void UPhysicsAsset::UpdateBodySetupIndexMap() {
        mBodySetupIndexMap.clear();

        for (Index_t Index{}; Index < mSkeletalBodySetups.size(); ++Index) {
            if (mSkeletalBodySetups[Index] != nullptr) {
                mBodySetupIndexMap[mSkeletalBodySetups[Index]->GetBoneName()] = Index;
            }
        }
    }
}
