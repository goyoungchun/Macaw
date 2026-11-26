/**
 * @file UPhysicsAsset.h
 * @brief 스켈레탈 메시의 뼈별 충돌 모양과 관절을 묶은 물리 에셋을 정의합니다.
 * @ingroup PhysicsRuntime
 */

#pragma once

#include "Core/Types.h"
#include "CoreUObject/UObject.h"
#include "PhysicsRuntime/UBodySetup.h"
#include "PhysicsRuntime/UPhysicsConstraintTemplate.h"

#include <memory>
#include <utility>

#include "UPhysicsAsset.generated.h"

namespace PhysicsRuntime {
    /**
     * @brief 스켈레탈 메시의 뼈별 충돌 모양과 관절을 묶은 물리 에셋입니다.
     * @ingroup PhysicsRuntime
     * @note 뼈는 이름으로만 가리키므로 스켈레톤 타입을 알지 않습니다.
     */
    UCLASS()
    class UPhysicsAsset : public CoreUObject::UObject {
        GENERATED_BODY()

    public:
        /**
         * @brief 바디와 관절이 없는 빈 에셋을 구성합니다.
         */
        UPhysicsAsset();

        /**
         * @brief 소유한 설계도와 관절 설정을 정리합니다.
         */
        ~UPhysicsAsset() override;

    public:
        /**
         * @brief 뼈 이름으로 바디 설계도의 번호를 찾습니다.
         * @param[in] BodyName 찾을 뼈의 이름입니다.
         * @return 설계도 번호이며, 없으면 InvalidIndex입니다.
         */
        Index_t FindBodyIndex(const Core::FString& BodyName) const;

        /**
         * @brief 관절 이름으로 관절 설정의 번호를 찾습니다.
         * @param[in] ConstraintName 찾을 관절의 이름입니다.
         * @return 관절 번호이며, 없으면 InvalidIndex입니다.
         */
        Index_t FindConstraintIndex(const Core::FString& ConstraintName) const;

        /**
         * @brief 뼈 이름에서 설계도 번호를 찾는 표를 다시 만듭니다.
         * @note 설계도를 추가하거나 지운 뒤 호출합니다.
         */
        void UpdateBodySetupIndexMap();

        /**
         * @brief 두 뼈의 바디 사이 충돌이 비활성으로 지정되어 있는지 반환합니다.
         * @param[in] BoneA 첫 번째 바디가 붙은 뼈의 이름입니다.
         * @param[in] BoneB 두 번째 바디가 붙은 뼈의 이름입니다.
         * @return 지정되어 있으면 true입니다. 순서는 상관없습니다.
         */
        bool IsCollisionDisabled(const Core::FString& BoneA, const Core::FString& BoneB) const;

        /**
         * @brief 두 뼈의 바디 사이 충돌을 비활성으로 지정하거나 해제합니다.
         * @param[in] BoneA 첫 번째 바디가 붙은 뼈의 이름입니다.
         * @param[in] BoneB 두 번째 바디가 붙은 뼈의 이름입니다.
         * @param[in] Disabled true이면 충돌을 끄고 false이면 켭니다.
         * @return 목록이 바뀌었으면 true입니다.
         */
        bool SetCollisionDisabled(const Core::FString& BoneA, const Core::FString& BoneB, bool Disabled);

        /**
         * @brief 충돌이 비활성인 쌍을 바디 번호의 쌍으로 바꿔 반환합니다. 바디가 없는 뼈의 쌍은 건너뜁니다.
         * @return 바디 번호의 쌍 목록이며 각 쌍은 (작은 번호, 큰 번호)입니다.
         */
        Core::TArray<std::pair<Uint32, Uint32>> BuildCollisionDisabledBodyPairs() const;

    public:
        /**
         * @brief 뼈마다 하나씩 가진 충돌 모양 설계도입니다.
         */
        Core::TArray<std::unique_ptr<UBodySetup>> mSkeletalBodySetups{};

        /**
         * @brief 두 뼈를 잇는 관절 설정입니다.
         */
        Core::TArray<std::unique_ptr<UPhysicsConstraintTemplate>> mConstraintSetup{};

        /**
         * @brief 서로 충돌하지 않게 지정한 바디 쌍이며 뼈 이름으로 가리킵니다. 관절로 이어진 쌍은 항상 충돌하지 않으므로 여기에 넣지 않아도 됩니다.
         */
        Core::TArray<std::pair<Core::FString, Core::FString>> mCollisionDisabledPairs{};

        /**
         * @brief 에디터가 이 에셋을 열 때 미리보기로 쓰는 스켈레탈 메시의 콘텐츠 경로입니다. 비어 있으면 지정하지 않은 것입니다.
         * @details 에셋 이름과 무관하므로 에셋 이름을 바꿔도 미리보기 메시를 잃지 않습니다. 시뮬레이션에는 쓰지 않습니다.
         */
        Core::FString mPreviewMeshPath{};

    private:
        /**
         * @brief 뼈 이름에서 설계도 번호를 찾는 표입니다.
         */
        Core::TMap<Core::FString, Index_t> mBodySetupIndexMap{};
    };
}
