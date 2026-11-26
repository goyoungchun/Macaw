/**
 * @file FPhysicsScene.h
 * @brief 월드 하나의 PhysX 장면과 시뮬레이션 결과를 관리합니다.
 * @ingroup PhysicsRuntime
 */

#pragma once

#include "Core/Literals/QuantityTypes.h"

#include "Core/Types.h"
#include "Core/Math/FVector.h"
#include "PhysicsRuntime/FBodyInstance.h"
#include "PhysicsRuntime/FPhysicsContact.h"
#include "PhysicsRuntime/FPhysXRelease.h"

#include <PxPhysicsAPI.h>
#include <memory>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace PhysicsRuntime {

    class FPhysicsRuntime;
    class FVehicleManager;

    /**
     * @brief 월드 하나의 PhysX 장면과 시뮬레이션 결과를 관리합니다.
     * @details 런타임이 만든 SDK 객체와 디스패처를 빌려 장면을 만들고, 한 스텝씩 동기로 시뮬레이션합니다.
     *          스텝마다 움직인 강체 목록과 새로 시작된 접촉을 보관하며, 장면 이벤트를 직접 수신합니다.
     * @note 이 객체에서 빌린 런타임보다 먼저 해제되어야 하며, 장면에 추가한 강체는 장면을 해제하기 전에 제거해야 합니다.
     * @ingroup PhysicsRuntime
     */
    class FPhysicsScene final : private physx::PxSimulationEventCallback, private physx::PxSimulationFilterCallback {
    public:

        /**
         * @brief 장면이 없는 상태로 구성합니다.
         */
        FPhysicsScene();

        /**
         * @brief 생성한 PhysX 장면을 해제합니다.
         */
        ~FPhysicsScene() override;

        /**
         * @brief 장면이 이벤트 수신자로 자신의 주소를 등록하므로 복사하지 않습니다.
         */
        FPhysicsScene(const FPhysicsScene&) = delete;

        /**
         * @brief 장면이 이벤트 수신자로 자신의 주소를 등록하므로 복사하지 않습니다.
         */
        FPhysicsScene& operator=(const FPhysicsScene&) = delete;

    public:

        /**
         * @brief 런타임의 SDK 객체로 PhysX 장면을 만듭니다.
         * @param[in] Runtime 초기화를 마친 PhysX 런타임입니다.
         * @return 이미 초기화되었거나 장면 생성에 성공하면 true, 그렇지 않으면 false입니다.
         */
        bool Initialize(PhysicsRuntime::FPhysicsRuntime& Runtime);

        /**
         * @brief PhysX 장면과 보관한 시뮬레이션 결과를 해제합니다.
         * @note 여러 번 호출해도 안전하며, 이 장면에 바디를 추가한 FBodyInstance는 먼저 해제되어야 합니다.
         */
        void Shutdown();

        /**
         * @brief PhysX 장면이 준비되었는지 반환합니다.
         * @return 장면이 있으면 true입니다.
         */
        bool IsInitialized() const;

        /**
         * @brief 장면을 만들 때 사용한 PhysX 런타임을 반환합니다.
         * @return 조회한 값의 비소유 주소이며, 초기화 전이면 nullptr입니다.
         */
        PhysicsRuntime::FPhysicsRuntime* GetRuntime() const;

        /** @brief 이 장면의 차량 관리자이며, 초기화 전에는 nullptr입니다. */
        PhysicsRuntime::FVehicleManager* GetVehicleManager() const;

        /**
         * @brief 장면을 한 스텝 진행하고 결과를 받을 때까지 기다립니다.
         * @param[in] StepSeconds 진행할 시간(초)이며 양의 유한값이어야 합니다.
         * @return 시뮬레이션을 진행했으면 true, 장면이 없거나 시간이 유효하지 않으면 false입니다.
         * @note 이전 스텝의 접촉 정보는 지우고 이번 스텝의 결과로 바꿉니다.
         */
        bool Simulate(Core::FTime Step);

        /**
         * @brief PhysX 장면의 비소유 주소를 반환합니다.
         * @return 조회한 값의 비소유 주소이며, 초기화 전이면 nullptr입니다.
         */
        physx::PxScene* GetScene() const;

        /**
         * @brief 같은 물리 에셋 인스턴스의 바디를 묶는 새 충돌 그룹 번호를 만듭니다.
         * @return 0이 아닌 고유한 번호입니다. 바디의 필터 데이터에 이 번호를 넣으면 같은 그룹의 바디끼리 쌍 목록으로 충돌 여부를 정합니다.
         */
        Uint32 AllocateCollisionGroup();

        /**
         * @brief 충돌 그룹 안에서 서로 충돌하지 않을 바디 쌍을 등록합니다.
         * @param[in] GroupId AllocateCollisionGroup이 반환한 그룹 번호입니다.
         * @param[in] Pairs 충돌하지 않을 바디 번호의 쌍 목록입니다.
         * @note 바디를 장면에 넣고 첫 시뮬레이션 전에 등록해야 합니다.
         */
        void SetCollisionDisabledPairs(Uint32 GroupId, const Core::TArray<std::pair<Uint32, Uint32>>& Pairs);

        /**
         * @brief 마지막 스텝에서 자세가 바뀐 강체 목록을 반환합니다.
         * @return 장면이 소유한 목록이며 다음 시뮬레이션 전까지 유효합니다. 장면이 없으면 빈 목록입니다.
         */
        std::span<physx::PxActor* const> GetActiveActors() const;

        /**
         * @brief 마지막 스텝에서 새로 시작된 접촉 목록을 반환합니다.
         * @return 이 객체가 보관하는 값의 참조이며 다음 시뮬레이션 전까지 유효합니다.
         */
        const Core::TArray<PhysicsRuntime::FPhysicsContact>& GetContacts() const;

        /**
         * @brief 구를 이동 경로로 쓸어 Block 응답 형상 중 처음 막힌 지점을 찾습니다.
         * @details 시작부터 겹친 형상은 법선을 믿을 수 없으므로 결과에서 뺍니다.
         * @param[in] Start 구 중심의 시작 위치(m)입니다.
         * @param[in] Delta 이동 벡터(m)입니다.
         * @param[in] Radius 구 반지름(m)이며 0이면 광선으로 쏩니다.
         * @param[out] OutHit 처음 막힌 지점의 정보입니다.
         * @param[in] ShouldIgnore true를 반환한 바디를 제외하며, 비어 있으면 쓰지 않습니다.
         * @return 막힌 지점이 있으면 true입니다.
         */
        bool SweepSphere(const FVector& Start, const FVector& Delta, Core::FLength Radius, PhysicsRuntime::FBodySweepHit& OutHit, const PhysicsRuntime::FIgnoreBodyPredicate& ShouldIgnore = {}) const;

    private:

        /**
         * @brief 같은 충돌 그룹의 바디 쌍이 비활성으로 등록되어 있으면 그 쌍을 충돌에서 제외합니다.
         */
        physx::PxFilterFlags pairFound(physx::PxU32 PairId, physx::PxFilterObjectAttributes Attributes0, physx::PxFilterData FilterData0, const physx::PxActor* Actor0, const physx::PxShape* Shape0, physx::PxFilterObjectAttributes Attributes1, physx::PxFilterData FilterData1, const physx::PxActor* Actor1, const physx::PxShape* Shape1, physx::PxPairFlags& PairFlags) override;

        /**
         * @brief 쌍이 사라져도 보관한 상태가 없으므로 아무것도 하지 않습니다.
         */
        void pairLost(physx::PxU32 PairId, physx::PxFilterObjectAttributes Attributes0, physx::PxFilterData FilterData0, physx::PxFilterObjectAttributes Attributes1, physx::PxFilterData FilterData1, bool ObjectRemoved) override;

        /**
         * @brief 추적 중인 쌍의 상태를 바꾸지 않습니다.
         */
        bool statusChange(physx::PxU32& PairId, physx::PxPairFlags& PairFlags, physx::PxFilterFlags& FilterFlags) override;

        /**
         * @brief 새로 시작된 접촉을 접촉 목록에 기록합니다.
         * @param[in] PairHeader 접촉한 두 강체의 정보입니다.
         * @param[in] Pairs 두 강체 사이의 형상 쌍별 접촉 정보입니다.
         * @param[in] PairCount 형상 쌍의 개수입니다.
         */
        void onContact(const physx::PxContactPairHeader& PairHeader, const physx::PxContactPair* Pairs, physx::PxU32 PairCount) override;

        /**
         * @brief 트리거 이벤트는 아직 사용하지 않습니다.
         */
        void onTrigger(physx::PxTriggerPair*, physx::PxU32) override;

        /**
         * @brief 조인트 파괴 이벤트는 아직 사용하지 않습니다.
         */
        void onConstraintBreak(physx::PxConstraintInfo*, physx::PxU32) override;

        /**
         * @brief 강체 깨어남 이벤트는 아직 사용하지 않습니다.
         */
        void onWake(physx::PxActor**, physx::PxU32) override;

        /**
         * @brief 강체 잠듦 이벤트는 아직 사용하지 않습니다.
         */
        void onSleep(physx::PxActor**, physx::PxU32) override;

        /**
         * @brief 시뮬레이션 도중 자세 전달 이벤트는 아직 사용하지 않습니다.
         */
        void onAdvance(const physx::PxRigidBody* const*, const physx::PxTransform*, const physx::PxU32) override;

    private:

        /**
         * @brief 이 객체가 단독 소유하는 PhysX 장면입니다.
         */
        std::unique_ptr<physx::PxScene, PhysicsRuntime::FPhysXRelease> mScene{};

        std::unique_ptr<PhysicsRuntime::FVehicleManager> mVehicleManager{};

        /**
         * @brief 장면과 바디가 SDK 객체를 빌리는 PhysX 런타임이며, 장면보다 오래 유지됩니다.
         */
        PhysicsRuntime::FPhysicsRuntime* mRuntime{};

        /**
         * @brief 마지막 스텝에서 새로 시작된 접촉 목록입니다.
         */
        Core::TArray<PhysicsRuntime::FPhysicsContact> mContacts{};

        /**
         * @brief 충돌 그룹별로 충돌하지 않을 바디 쌍을 보관합니다. 키는 (작은 번호 << 32) | 큰 번호입니다.
         */
        std::unordered_map<Uint32, std::unordered_set<Uint64>> mDisabledPairs{};

        /**
         * @brief 마지막으로 만든 충돌 그룹 번호입니다.
         */
        Uint32 mLastCollisionGroup{};
    };

}
