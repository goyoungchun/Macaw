/**
 * @file ClothEditing.h
 * @brief 에디터 명령과 메시 뷰어가 함께 쓰는 천 에셋 만들기, 빼기, 설정 목록을 선언합니다.
 * @ingroup Editor
 */

#pragma once

#include "Core/Types.h"
#include "Core/Literals/QuantityTypes.h"
#include "Core/Math/FVector.h"
// 설정 멤버 포인터의 크기가 파일마다 같도록, 앞선언 대신 정의를 include합니다.
#include "PhysicsRuntime/UClothConfigNv.h"

#include <utility>

namespace Asset {
    class USkeletalMesh;
    class USkeleton;
}

namespace PhysicsRuntime {
    class UClothingAssetCommon;
    class UPhysicsAsset;
    struct FClothPhysicalMeshData;
    enum class EWeightMapTargetCommon : Uint8;
}

namespace Editor {

    /**
     * @brief 에디터 명령과 메시 뷰어가 함께 쓰는 천 편집 함수 모음입니다.
     * @ingroup Editor
     */
    namespace ClothEditing {
        /**
         * @brief 천 설정 항목 하나의 이름, 묶음, 단위, 범위, 기본값, 설명입니다.
         */
        struct FClothConfigField {
        public:
            bool HasScalarValue() const;
            float GetScalarValue(PhysicsRuntime::UClothConfigNv& Config) const;
            void SetScalarValue(PhysicsRuntime::UClothConfigNv& Config, float BaseValue) const;

        public:
            /**
             * @brief 콘솔 명령에서 쓰는 소문자 이름입니다.
             */
            const char* mKey{};

            /**
             * @brief 패널에 보여 줄 이름입니다.
             */
            const char* mLabel{};

            /**
             * @brief 패널에서 함께 묶어 보여 줄 묶음 이름입니다.
             */
            const char* mGroup{};

            /**
             * @brief 값 칸의 표시 형식이며 단위를 포함합니다.
             */
            const char* mFormat{};

            /**
             * @brief 저장 값에 곱해 보여 줄 배율이며 미터를 센티미터로 보일 때 100입니다.
             */
            float mDisplayScale{1.0f};

            /**
             * @brief 저장 단위의 최솟값입니다.
             */
            float mMin{};

            /**
             * @brief 저장 단위의 최댓값입니다.
             */
            float mMax{};

            /**
             * @brief 표시 단위로 마우스 한 픽셀에 바뀌는 양입니다.
             */
            float mSpeed{};

            /**
             * @brief 기본값이며 실수 항목은 x만 씁니다.
             */
            FVector mDefault{};

            /**
             * @brief 무엇을 하는지와 올리면 어떻게 되는지만 적은 짧은 한국어 설명이며, 기본값과 범위는 패널이 덧붙입니다.
             */
            const char* mTooltip{};

            /**
             * @brief 실수 항목의 값 위치를 구하는 함수이며 벡터 항목이면 nullptr입니다.
             */
            float* (*mGetFloat)(PhysicsRuntime::UClothConfigNv&){};

            /**
             * @brief 벡터 항목의 값 위치를 구하는 함수이며 실수 항목이면 nullptr입니다.
             */
            FVector* (*mGetVector)(PhysicsRuntime::UClothConfigNv&){};

            Core::FLength PhysicsRuntime::UClothConfigNv::* mLength{};
            Core::FFrequency PhysicsRuntime::UClothConfigNv::* mFrequency{};
        };

        /**
         * @brief 이름과 설명을 함께 가진 표시 항목입니다.
         */
        struct FClothLabel {
            /**
             * @brief 패널에 보여 줄 이름입니다.
             */
            const char* mLabel{};

            /**
             * @brief 한국어 설명입니다.
             */
            const char* mTooltip{};
        };

        /**
         * @brief 칠하기 대상 하나의 이름, 단위, 범위, 설명입니다.
         */
        struct FPaintTargetInfo {
            /**
             * @brief 패널에 보여 줄 이름입니다.
             */
            const char* mName{};

            /**
             * @brief 값 칸의 표시 형식이며 단위를 포함합니다.
             */
            const char* mFormat{};

            /**
             * @brief 붓 값 칸의 최댓값입니다.
             */
            float mMax{};

            /**
             * @brief 마우스 한 픽셀에 바뀌는 양입니다.
             */
            float mSpeed{};

            /**
             * @brief 무엇을 칠하는지와 값의 뜻을 적은 한국어 설명입니다.
             */
            const char* mTooltip{};
        };

        /**
         * @brief 천 가중치를 칠하는 도구입니다.
         */
        enum class EClothPaintTool : Uint8 {
            /**
             * @brief 붓 안의 입자에 값을 그대로 칠합니다.
             */
            Brush,

            /**
             * @brief 붓 안의 입자 값을 이웃 입자 값의 평균 쪽으로 다듬습니다.
             */
            Smooth,

            /**
             * @brief 누른 입자에서 시작해 값이 비슷한 이웃 입자까지 한 번에 채웁니다.
             */
            Fill
        };

        /**
         * @brief 모든 천 설정 항목을 패널 표시 순서로 반환합니다.
         * @return 강성 항목은 세로, 가로, 굽힘, 전단 순서로 네 칸씩 앞에 있습니다.
         */
        const Core::TArray<FClothConfigField>& GetConfigFields();

        /**
         * @brief 강성 표의 행 이름과 설명을 세로, 가로, 굽힘, 전단 순서로 반환합니다.
         * @return 네 행의 이름과 설명입니다.
         */
        const Core::TArray<FClothLabel>& GetStiffnessRows();

        /**
         * @brief 강성 표의 열 이름과 설명을 세기, 배율, 늘어남 한계, 줄어듦 한계 순서로 반환합니다.
         * @return 네 열의 이름과 설명입니다.
         */
        const Core::TArray<FClothLabel>& GetStiffnessColumns();

        /**
         * @brief 강성 표가 아닌 설정 묶음의 이름과 설명을 패널 표시 순서로 반환합니다.
         * @return 묶음 이름과 설명입니다.
         */
        const Core::TArray<FClothLabel>& GetConfigGroups();

        /**
         * @brief 바람 방식 선택 칸의 설명을 반환합니다.
         * @return 한국어 설명입니다.
         */
        const char* GetWindMethodTooltip();

        /**
         * @brief 강성 표 묶음의 이름입니다.
         */
        inline constexpr const char* StiffnessGroup{"Stiffness"};
        /**
         * @brief 스켈레톤의 뼈 이름을 뼈 번호 순서로 모읍니다.
         * @param[in] Skeleton 뼈를 가진 스켈레톤입니다.
         * @return 뼈 번호 순서의 뼈 이름입니다.
         */
        Core::TArray<Core::FString> GetSkeletonBoneNames(const Asset::USkeleton& Skeleton);

        /**
         * @brief 섹션 하나로 천 에셋을 만들어 메시에 넣고 섹션에 연결합니다.
         * @details 최대 거리를 칠하기 전에도 움직이도록 위쪽 일부 입자만 고정하는 초기값을 넣습니다.
         * @param[in,out] Mesh 섹션을 가진 메시입니다.
         * @param[in] Skeleton 메시가 쓰는 스켈레톤입니다.
         * @param[in] SubMeshIndex 천으로 바꿀 섹션 번호입니다.
         * @param[in] PhysicsAsset 천이 부딪힐 몸을 담은 물리 에셋이며 nullptr이면 부딪히지 않습니다.
         * @param[in] PhysicsAssetPath 물리 에셋의 Content 상대 경로이며 저장하지 않은 물리 에셋이면 비워 둡니다.
         * @param[out] Error 실패 원인입니다.
         * @return 만들고 연결한 천 에셋이며 실패하면 nullptr입니다.
         */
        PhysicsRuntime::UClothingAssetCommon* CreateClothFromSection(Asset::USkeletalMesh& Mesh, const Asset::USkeleton& Skeleton, Index_t SubMeshIndex, PhysicsRuntime::UPhysicsAsset* PhysicsAsset, const Core::FString& PhysicsAssetPath, Core::FString& Error);

        /**
         * @brief 천이 연결된 섹션의 렌더 정점과 천 입자 사이 연결 데이터를 지금 계산 방식으로 다시 만듭니다.
         * @details 칠한 값과 설정은 그대로 두고, 섹션 정점이 천 삼각형 어디에 붙는지만 다시 구합니다.
         * @param[in,out] Mesh 섹션을 가진 메시입니다.
         * @param[in] SubMeshIndex 천이 연결된 섹션 번호입니다.
         * @param[out] Error 실패 원인입니다.
         * @return 다시 만들었으면 true입니다.
         */
        bool RebuildSectionMapping(Asset::USkeletalMesh& Mesh, Index_t SubMeshIndex, Core::FString& Error);

        /**
         * @brief 설정의 벡터 항목 이름과 값 위치를 모읍니다.
         * @param[in] Config 값을 가진 설정입니다.
         * @return 소문자 이름과 값 위치입니다.
         */
        Core::TArray<std::pair<Core::FString, FVector*>> GetConfigVectors(PhysicsRuntime::UClothConfigNv& Config);

        /**
         * @brief 칠할 수 있는 가중치 맵 대상의 수입니다.
         */
        inline constexpr int NumPaintTargets{4};

        /**
         * @brief 칠하기 대상 번호의 표시 이름을 반환합니다.
         * @param[in] TargetIndex 0부터 NumPaintTargets - 1 사이의 대상 번호입니다.
         * @return 표시 이름입니다.
         */
        const char* GetPaintTargetName(int TargetIndex);

        /**
         * @brief 칠하기 대상 번호를 가중치 맵 대상으로 바꿉니다.
         * @param[in] TargetIndex 0부터 NumPaintTargets - 1 사이의 대상 번호입니다.
         * @return 가중치 맵 대상입니다.
         */
        PhysicsRuntime::EWeightMapTargetCommon GetPaintTarget(int TargetIndex);

        /**
         * @brief 칠하기 대상의 이름, 단위, 범위, 설명을 반환합니다.
         * @param[in] TargetIndex 0부터 NumPaintTargets - 1 사이의 대상 번호입니다.
         * @return 칠하기 대상 정보입니다.
         */
        const FPaintTargetInfo& GetPaintTargetInfo(int TargetIndex);

        /**
         * @brief 칠하기 도구의 이름과 설명을 붓, 다듬기, 채우기 순서로 반환합니다.
         * @return 세 도구의 이름과 설명입니다.
         */
        const Core::TArray<FClothLabel>& GetPaintTools();

        /**
         * @brief 시뮬레이션 메시의 삼각형으로 입자마다 변으로 이어진 이웃 입자를 모읍니다.
         * @param[in] PhysMesh 입자와 삼각형을 가진 시뮬레이션 메시입니다.
         * @return 입자 번호마다 이웃 입자 번호 목록입니다.
         */
        Core::TArray<Core::TArray<Uint32>> BuildParticleNeighbors(const PhysicsRuntime::FClothPhysicalMeshData& PhysMesh);

        /**
         * @brief 고른 입자의 값을 이웃 입자 값의 평균 쪽으로 다듬습니다.
         * @param[in,out] Values 입자마다 가중치 값입니다.
         * @param[in] Neighbors 입자마다 이웃 입자 번호 목록입니다.
         * @param[in] Particles 다듬을 입자 번호입니다.
         * @param[in] Strength 0이면 그대로 두고 1이면 이웃 평균으로 바꾸는 비율입니다.
         */
        void SmoothWeights(Core::TArray<float>& Values, const Core::TArray<Core::TArray<Uint32>>& Neighbors, const Core::TArray<Index_t>& Particles, float Strength);

        /**
         * @brief 시작 입자에서 이웃을 따라가며 시작 값과 차이가 기준 이하인 입자를 한 값으로 채웁니다.
         * @param[in,out] Values 입자마다 가중치 값입니다.
         * @param[in] Neighbors 입자마다 이웃 입자 번호 목록입니다.
         * @param[in] StartParticle 채우기를 시작할 입자 번호입니다.
         * @param[in] Value 채울 값입니다.
         * @param[in] Threshold 시작 값과 이만큼까지 다른 입자를 채웁니다.
         * @return 값을 바꾼 입자 수입니다.
         */
        Index_t FillWeights(Core::TArray<float>& Values, const Core::TArray<Core::TArray<Uint32>>& Neighbors, Index_t StartParticle, float Value, float Threshold);

        /**
         * @brief 칠한 가중치 맵을 천 에셋에 반영합니다.
         * @details 편집용 가중치 맵을 맞추고 역질량, 천끼리 부딪힐 입자, 섹션 정점의 스키닝만 쓰는 표시를 다시 구합니다.
         * @param[in,out] Mesh 천 에셋을 가진 메시입니다.
         * @param[in,out] ClothingAsset 칠한 천 에셋입니다.
         */
        void ApplyPaintedWeights(Asset::USkeletalMesh& Mesh, PhysicsRuntime::UClothingAssetCommon& ClothingAsset);
    }
}
