/**
 * @file FRigidBodySimulation.h
 * @brief 피직스 에셋의 바디와 관절로 월드와 분리된 독립 물리 장면을 만들어 고정 스텝으로 시뮬레이션합니다.
 * @ingroup PhysicsRuntime
 */

#pragma once

#include "Core/Types.h"
#include "Core/Literals/QuantityTypes.h"
#include "Core/Math/FTransform.h"
#include "PhysicsRuntime/FBodyInstance.h"
#include "PhysicsRuntime/FConstraintInstance.h"
#include "PhysicsRuntime/FPhysicsScene.h"

#include <functional>
#include <memory>

namespace PhysicsRuntime {

    class FPhysicsRuntime;
    class UPhysicsAsset;

    /**
     * @brief 피직스 에셋의 바디와 관절로 월드와 분리된 독립 물리 장면을 만들어 고정 스텝으로 시뮬레이션합니다.
     * @details 에셋에서 Kinematic으로 지정한 바디는 호출자가 준 목표 변환을 따라가고, Dynamic 바디는 관절로 이어져 시뮬레이션됩니다.
     *          머리카락이나 장신구처럼 캐릭터의 일부만 독립적으로 시뮬레이션하는 애니메이션 노드가 쓰며, 월드의 다른 바디와는 충돌하지 않습니다.
     * @note 장면과 바디, 관절은 이 객체가 소유하며 선언 순서의 역순으로 해제됩니다. 이 장면의 바디끼리는 필터 없이 모두 충돌하므로,
     *       처음부터 겹쳐 있는 서로 다른 바디(예: 머리 Kinematic 바디와 머리카락 Dynamic 바디)는 관절로 잇거나 겹치지 않게 만들어야 합니다.
     * @ingroup PhysicsRuntime
     */
    class FRigidBodySimulation {
    public:

        /**
         * @brief 뼈 이름으로 그 뼈의 현재 월드 변환(스케일 포함)을 구하는 함수입니다. 뼈를 찾지 못하면 false를 반환합니다.
         */
        using FBoneWorldFunction = std::function<bool(const Core::FString& BoneName, FTransform& OutWorld)>;

    public:

        /**
         * @brief 장면이 없는 상태로 구성합니다.
         */
        FRigidBodySimulation();

        /**
         * @brief 관절, 바디, 장면의 순서로 해제합니다.
         */
        ~FRigidBodySimulation();

        /**
         * @brief 장면과 바디가 서로 참조하므로 복사하지 않습니다.
         */
        FRigidBodySimulation(const FRigidBodySimulation&) = delete;

        /**
         * @brief 장면과 바디가 서로 참조하므로 복사하지 않습니다.
         */
        FRigidBodySimulation& operator=(const FRigidBodySimulation&) = delete;

    public:

        /**
         * @brief 독립 물리 장면을 만들고 피직스 에셋의 바디와 관절을 현재 자세에 만듭니다.
         * @param[in] Runtime 초기화를 마친 PhysX 런타임이며 이 객체보다 오래 유지되어야 합니다.
         * @param[in] Asset 바디 설명서와 관절 설정을 가진 피직스 에셋입니다. 생성 중에만 읽습니다.
         * @param[in] GetBoneWorld 바디가 붙은 뼈의 월드 변환을 구하는 함수입니다. 뼈를 찾지 못한 바디는 만들지 않습니다.
         * @param[in] Scale3D 바디와 관절에 반영할 컴포넌트 스케일입니다.
         * @return 장면을 만들고 바디를 하나 이상 만들었으면 true입니다. 실패하면 아무것도 남기지 않습니다.
         * @note 이미 초기화되어 있으면 먼저 Shutdown을 호출한 뒤 다시 만듭니다. 생성 시점의 자세가 곧 시작 자세이므로 맞는 자세로 호출해야 합니다.
         */
        bool Initialize(PhysicsRuntime::FPhysicsRuntime& Runtime, const PhysicsRuntime::UPhysicsAsset& Asset, const FBoneWorldFunction& GetBoneWorld, const FVector& Scale3D);

        /**
         * @brief 관절, 바디, 장면을 해제합니다. 여러 번 호출해도 안전합니다.
         */
        void Shutdown();

        /**
         * @brief 장면과 바디가 만들어져 있는지 반환합니다.
         * @return 시뮬레이션할 수 있으면 true입니다.
         */
        bool IsInitialized() const;

        /**
         * @brief 에셋의 바디 설명서 개수(번호 범위)를 반환합니다.
         * @return 번호는 0부터 이 값 미만이며, 만들지 못한 번호도 포함합니다.
         */
        Index_t GetBodyCount() const;

        /**
         * @brief 바디가 실제로 만들어져 있는지 반환합니다.
         * @param[in] BodyIndex 피직스 에셋 안의 바디 번호입니다.
         * @return 만들어져 있으면 true입니다.
         */
        bool HasBody(Index_t BodyIndex) const;

        /**
         * @brief 바디가 붙은 뼈의 이름을 반환합니다.
         * @param[in] BodyIndex 피직스 에셋 안의 바디 번호입니다.
         * @return 뼈 이름이며, 번호가 범위를 벗어나면 빈 문자열입니다.
         */
        const Core::FString& GetBoneName(Index_t BodyIndex) const;

        /**
         * @brief 바디가 Dynamic으로 시뮬레이션되는지 반환합니다.
         * @param[in] BodyIndex 피직스 에셋 안의 바디 번호입니다.
         * @return Dynamic이면 true이고 Kinematic이거나 없는 바디면 false입니다.
         */
        bool IsSimulated(Index_t BodyIndex) const;

        /**
         * @brief Kinematic 바디가 다음 Advance에서 따라갈 월드 변환을 지정합니다.
         * @param[in] BodyIndex 피직스 에셋 안의 바디 번호입니다.
         * @param[in] World 바디의 목표 월드 변환입니다.
         * @note 처음 지정하면 이전 목표도 같은 값으로 두어 갑자기 튀지 않습니다. Dynamic 바디나 없는 바디는 무시합니다.
         */
        void SetKinematicTarget(Index_t BodyIndex, const FTransform& World);

        /**
         * @brief 바디가 반응하는 정도(UBodySetup의 WindInfluence)를 반환합니다.
         * @param[in] BodyIndex 피직스 에셋 안의 바디 번호입니다.
         * @return 바람 영향 계수(1/s)이며, 없는 바디나 Kinematic 바디면 0입니다.
         */
        float GetWindInfluence(Index_t BodyIndex) const;

        /**
         * @brief Dynamic 바디 위치의 바람 속도를 지정합니다. 다음 Advance의 모든 스텝에서 같은 값을 씁니다.
         * @param[in] BodyIndex 피직스 에셋 안의 바디 번호입니다.
         * @param[in] WindVelocity 월드 좌표의 바람 속도(m/s)입니다. 영벡터면 바람이 없는 것입니다.
         * @note 스텝마다 `(바람 속도 - 바디 속도) × 바람 영향`을 가속도로 바디에 줍니다. 바람 영향이 0이거나 Kinematic 바디면 무시합니다.
         */
        void SetBodyWind(Index_t BodyIndex, const FVector& WindVelocity);

        /**
         * @brief 경과 시간을 누적해 고정 스텝만큼 시뮬레이션합니다.
         * @param[in] DeltaSeconds 이번에 더할 경과 시간(초)입니다.
         * @note 스텝 수는 런타임 설정의 고정 스텝 간격과 프레임당 최대 스텝 수를 따릅니다. 한 프레임에 스텝이 여러 번이면
         *       Kinematic 바디의 목표는 이전 목표에서 현재 목표까지 보간해 스텝마다 주어, 마지막 스텝에서만 움직이지 않게 합니다.
         *       스텝이 0번이면 목표만 보관하고 바디는 움직이지 않습니다.
         */
        void Advance(Core::FTime DeltaTime);

        /**
         * @brief 바디의 현재 월드 변환을 구합니다.
         * @param[in] BodyIndex 피직스 에셋 안의 바디 번호입니다.
         * @param[out] OutWorld 바디의 월드 변환이며 스케일은 만들 때 쓴 값입니다.
         * @return 바디가 있으면 true입니다.
         */
        bool GetBodyTransform(Index_t BodyIndex, FTransform& OutWorld) const;

    private:

        /**
         * @brief 바디 설명서 하나에 대응하는 바디와 Kinematic 목표입니다.
         */
        struct FEntry {
            /**
             * @brief 장면에 만든 바디이며 만들지 못했으면 비어 있습니다.
             */
            std::unique_ptr<PhysicsRuntime::FBodyInstance> mBody{};

            /**
             * @brief 바디가 붙은 뼈의 이름입니다.
             */
            Core::FString mBoneName{};

            /**
             * @brief Dynamic으로 시뮬레이션하는지 나타냅니다.
             */
            bool mSimulated{};

            /**
             * @brief 마지막 스텝에서 적용한 Kinematic 목표입니다.
             */
            FTransform mPreviousTarget{};

            /**
             * @brief 다음 스텝에 적용할 Kinematic 목표입니다.
             */
            FTransform mTarget{};

            /**
             * @brief 목표를 한 번이라도 지정했는지 나타냅니다.
             */
            bool mHasTarget{};

            /**
             * @brief 바람에 반응하는 정도(1/s)입니다. 0이면 바람을 받지 않습니다.
             */
            float mWindInfluence{};

            /**
             * @brief 이 바디 위치의 바람 속도(m/s)입니다. 호출자가 Advance 전에 지정합니다.
             */
            FVector mWind{};
        };

    private:

        /**
         * @brief 이 시뮬레이션이 단독 소유하는 물리 장면입니다. 바디와 관절보다 먼저 선언해 가장 늦게 해제됩니다.
         */
        std::unique_ptr<PhysicsRuntime::FPhysicsScene> mScene{};

        /**
         * @brief 바디 설명서 번호 순서의 항목입니다.
         */
        Core::TArray<FEntry> mEntries{};

        /**
         * @brief 두 바디를 잇는 관절입니다. 바디보다 나중에 선언해 먼저 해제됩니다.
         */
        Core::TArray<std::unique_ptr<PhysicsRuntime::FConstraintInstance>> mConstraints{};

        /**
         * @brief 아직 스텝으로 쓰지 않은 누적 시간(초)입니다.
         */
        Core::FTime mAccumulator{};
    };

}
