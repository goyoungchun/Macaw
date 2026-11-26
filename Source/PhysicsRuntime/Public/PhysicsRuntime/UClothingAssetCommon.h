/**
 * @file UClothingAssetCommon.h
 * @brief 스켈레탈 메시의 섹션 하나를 천으로 시뮬레이션할 데이터를 담은 에셋을 선언합니다.
 * @ingroup PhysicsRuntime
 */

#pragma once

#include "Core/Types.h"
#include "CoreUObject/TObjectRef.h"
#include "CoreUObject/UObject.h"
#include "PhysicsRuntime/FClothLODDataCommon.h"
#include "PhysicsRuntime/UClothConfigNv.h"
#include "PhysicsRuntime/UPhysicsAsset.h"

#include <memory>

#include "UClothingAssetCommon.generated.h"

namespace Core {
    class FArchive;
}

namespace PhysicsRuntime {

    /**
     * @brief 스켈레탈 메시의 섹션 하나를 천으로 시뮬레이션할 데이터를 담은 에셋입니다.
     * @details 스켈레탈 메시가 소유하며, 상세 단계마다 시뮬레이션 메시를 하나씩 가집니다.
     *          뼈는 이름으로 저장하고 메시에 연결할 때 메시의 뼈 번호를 다시 찾습니다.
     * @ingroup PhysicsRuntime
     */
    UCLASS()
    class UClothingAssetCommon : public CoreUObject::UObject {
        GENERATED_BODY()

    public:

        /**
         * @brief 상세 단계가 없는 빈 에셋을 구성합니다.
         */
        UClothingAssetCommon();

        /**
         * @brief 상세 단계 데이터를 정리합니다.
         */
        ~UClothingAssetCommon() override;

    public:

        /**
         * @brief 빈 상세 단계를 하나 추가합니다.
         * @return 추가한 상세 단계의 번호입니다.
         */
        Index_t AddNewLod();

        /**
         * @brief 상세 단계 번호가 유효한지 검사합니다.
         * @param[in] LodIndex 검사할 번호입니다.
         * @return 유효하면 true입니다.
         */
        bool IsValidLod(Index_t LodIndex) const;

        /**
         * @brief 상세 단계 수를 반환합니다.
         * @return 상세 단계 수입니다.
         */
        Index_t GetNumLods() const;

        /**
         * @brief 시뮬레이션 메시에서 계산하는 값을 다시 구합니다.
         * @details 삼각형 넓이로 역질량을 구하고, 최대 거리가 0인 입자는 역질량 0으로 고정합니다.
         *          입자마다 가중치가 0이 아닌 뼈 영향 수도 다시 셉니다.
         */
        void InvalidateCachedData();

        /**
         * @brief 설정의 천끼리 충돌 거리로 상세 단계마다 천끼리 부딪힐 입자를 다시 고릅니다.
         */
        void BuildSelfCollisionData();

        /**
         * @brief 천 설정을 반환합니다.
         * @return 에셋이 소유하는 설정입니다.
         */
        UClothConfigNv* GetClothConfig();

        /**
         * @brief 천 설정을 반환합니다.
         * @return 에셋이 소유하는 설정입니다.
         */
        const UClothConfigNv* GetClothConfig() const;

        /**
         * @brief 사용 뼈 이름마다 스켈레톤의 뼈 번호를 다시 찾습니다.
         * @param[in] SkeletonBoneNames 스켈레톤의 뼈 이름이며 순서가 뼈 번호입니다.
         */
        void RefreshBoneMapping(const Core::TArray<Core::FString>& SkeletonBoneNames);

        /**
         * @brief 천이 부딪힐 몸을 담은 물리 에셋을 정하고, 그 몸통 뼈도 사용 뼈에 넣어 뼈 번호를 다시 찾습니다.
         * @details 천 입자에 영향이 없는 뼈라도 충돌체가 그 뼈를 따라 움직여야 하므로 사용 뼈에 넣습니다.
         * @param[in] PhysicsAsset 천이 부딪힐 몸이며 nullptr이면 물리 에셋 충돌을 쓰지 않습니다.
         * @param[in] SkeletonBoneNames 스켈레톤의 뼈 이름이며 순서가 뼈 번호입니다.
         */
        void SetPhysicsAsset(UPhysicsAsset* PhysicsAsset, const Core::TArray<Core::FString>& SkeletonBoneNames);

        /**
         * @brief 식별자, 상세 단계 데이터, 사용 뼈, 물리 에셋 경로, 설정을 저장하거나 불러옵니다.
         * @details 물리 에셋은 경로만 저장하므로, 불러온 뒤 에셋 관리자가 경로로 찾아 mPhysicsAsset에 넣습니다.
         * @param[in,out] Archive 저장 또는 불러오기 방향을 가진 아카이브입니다.
         */
        void SerializeClothingData(Core::FArchive& Archive);

        /**
         * @brief 시뮬레이션 좌표의 기준이 될 뼈를 정합니다.
         * @note NvCloth는 컴포넌트 좌표에서 시뮬레이션하므로 항상 루트 뼈를 씁니다.
         */
        void CalculateReferenceBoneIndex();

        /**
         * @brief 이웃한 상세 단계 사이에서 입자 위치를 옮길 데이터를 만듭니다.
         */
        void BuildLodTransitionData();

    public:

        /**
         * @brief 상세 단계별 시뮬레이션 데이터입니다.
         */
        Core::TArray<FClothLODDataCommon> mLodData{};

        /**
         * @brief 메시의 상세 단계 번호로 이 에셋의 상세 단계 번호를 찾는 표이며 없으면 InvalidIndex입니다.
         */
        Core::TArray<Index_t> mLodMap{};

        /**
         * @brief 입자 뼈 영향이 가리키는 뼈 이름입니다.
         */
        Core::TArray<Core::FString> mUsedBoneNames{};

        /**
         * @brief 사용 뼈 이름 순서대로 찾은 스켈레톤 뼈 번호이며 없으면 InvalidIndex입니다.
         */
        Core::TArray<Index_t> mUsedBoneIndices{};

        /**
         * @brief 시뮬레이션 좌표의 기준이 되는 스켈레톤 뼈 번호입니다.
         */
        Index_t mReferenceBoneIndex{};

        /**
         * @brief 천이 부딪힐 몸을 담은 물리 에셋이며 소유하지 않습니다.
         * @details 물리 에셋이 먼저 사라지면 비어 있는 것으로 보고 충돌을 쓰지 않습니다.
         */
        CoreUObject::TObjectRef<UPhysicsAsset> mPhysicsAsset{};

        /**
         * @brief 천이 부딪힐 물리 에셋의 Content 상대 경로이며, 저장하지 않은 물리 에셋이면 비어 있습니다.
         */
        Core::FString mPhysicsAssetPath{};

    private:

        /**
         * @brief 천을 만들 때 NvCloth에 넣는 설정입니다.
         */
        std::unique_ptr<UClothConfigNv> mClothConfig{};
    };
}
