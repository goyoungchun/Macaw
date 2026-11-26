/**
 * @file FPhysicsScene.cpp
 * @brief 월드 하나의 PhysX 장면과 시뮬레이션 결과를 관리합니다.
 * @ingroup PhysicsRuntime
 */

#include "Core/Types.h"
#include "PhysicsRuntime/FPhysicsScene.h"
#include "PhysicsRuntime/CollisionTypes.h"
#include "PhysicsRuntime/FPhysicsRuntime.h"
#include "PhysicsRuntime/FVehicleManager.h"
#include "PhysicsRuntime/PhysicsConversion.h"
#include "PhysicsRuntime/PhysicsStats.h"
#include "Core/Log/Log.h"

#include <algorithm>
#include <cmath>

namespace PhysicsRuntime {

    /**
     * @brief 장면 생성에 쓰는 충돌 필터와 접촉 수집 설정입니다.
     */
    namespace {
        DEFINE_LOG_CATEGORY_STATIC(LogPhysics, Log, All);

        /**
         * @brief 형상 쌍 하나에서 읽을 최대 접촉점 수입니다.
         */
        constexpr physx::PxU32 MaxContactPointsPerPair{8};

        /**
         * @brief 바디가 충돌하지 않도록 표시하는 필터 데이터 word3의 값입니다.
         */
        constexpr physx::PxU32 NoCollisionMarker{1};

        /**
         * @brief 충돌 그룹 안의 바디 쌍을 하나의 키로 만듭니다. 순서와 상관없이 같은 키가 나옵니다.
         */
        Uint64 MakePairKey(physx::PxU32 A, physx::PxU32 B) {
            return (static_cast<Uint64>(std::min(A, B)) << 32) | static_cast<Uint64>(std::max(A, B));
        }

        /**
         * @brief 충돌이 꺼진 바디의 쌍을 제외하고, 두 형상의 응답이 모두 Block인 쌍만 충돌시키며 새 접촉과 CCD 접촉을 알리도록 지정합니다.
         * @param[in] Attributes0 첫 번째 형상의 필터 속성입니다.
         * @param[in] FilterData0 첫 번째 형상의 필터 데이터입니다. word0은 충돌 응답, word1은 충돌 그룹, word2는 그룹 안의 바디 번호, word3은 충돌 끔 표시입니다.
         * @param[in] Attributes1 두 번째 형상의 필터 속성입니다.
         * @param[in] FilterData1 두 번째 형상의 필터 데이터입니다.
         * @param[out] PairFlags 형상 쌍에 적용할 접촉 처리 플래그입니다.
         * @return Block 쌍이면 기본 필터 플래그이고, 같은 충돌 그룹의 쌍은 쌍 목록을 보도록 콜백으로 넘깁니다. 충돌 끔 바디가 있으면 제외하고, Block이 아니면 접촉을 만들지 않는 억제 플래그입니다.
         * @note 겹침은 시뮬레이션이 아니라 장면 쿼리로 찾으므로, Overlap 쌍도 접촉을 만들지 않습니다.
         */
        physx::PxFilterFlags SimulationFilterShader(physx::PxFilterObjectAttributes Attributes0, physx::PxFilterData FilterData0, physx::PxFilterObjectAttributes Attributes1, physx::PxFilterData FilterData1, physx::PxPairFlags& PairFlags, const void*, physx::PxU32) {
            if (physx::PxFilterObjectIsTrigger(Attributes0) || physx::PxFilterObjectIsTrigger(Attributes1)) {
                PairFlags = physx::PxPairFlag::eTRIGGER_DEFAULT;

                return physx::PxFilterFlag::eDEFAULT;
            }

            // 바디별로 충돌을 끈 경우: 어느 한쪽이라도 표시되어 있으면 월드와 같은 에셋의 모든 상대와 접촉을 만들지 않는다.
            if (FilterData0.word3 == NoCollisionMarker || FilterData1.word3 == NoCollisionMarker) {
                return physx::PxFilterFlag::eKILL;
            }

            const auto Response{PhysicsRuntime::CombineCollisionResponse(static_cast<PhysicsRuntime::ECollisionResponse>(FilterData0.word0), static_cast<PhysicsRuntime::ECollisionResponse>(FilterData1.word0))};

            if (Response != PhysicsRuntime::ECollisionResponse::Block) {
                return physx::PxFilterFlag::eSUPPRESS;
            }

            PairFlags = physx::PxPairFlag::eCONTACT_DEFAULT | physx::PxPairFlag::eDETECT_CCD_CONTACT | physx::PxPairFlag::eNOTIFY_TOUCH_FOUND | physx::PxPairFlag::eNOTIFY_CONTACT_POINTS;

            // 같은 물리 에셋 인스턴스의 두 바디는 비활성 쌍 목록을 봐야 하므로 콜백이 정한다.
            if (FilterData0.word1 != 0 && FilterData0.word1 == FilterData1.word1) {
                return physx::PxFilterFlag::eCALLBACK;
            }

            return physx::PxFilterFlag::eDEFAULT;
        }
    }

    /**
     * @brief 장면이 없는 상태로 구성합니다.
     */
    PhysicsRuntime::FPhysicsScene::FPhysicsScene() = default;

    /**
     * @brief 생성한 PhysX 장면을 해제합니다.
     */
    PhysicsRuntime::FPhysicsScene::~FPhysicsScene() {
        Shutdown();
    }

    /**
     * @brief 런타임의 SDK 객체로 PhysX 장면을 만듭니다.
     */
    bool PhysicsRuntime::FPhysicsScene::Initialize(PhysicsRuntime::FPhysicsRuntime& Runtime) {
        if (IsInitialized()) {
            return true;
        }

        if (!Runtime.IsInitialized()) {
            MACAW_LOG(LogPhysics, Error, "Cannot create a physics scene before the PhysX runtime is initialized.");

            return false;
        }

        const PhysicsRuntime::FPhysicsSettings& Settings{Runtime.GetSettings()};

        physx::PxSceneDesc Desc{Runtime.GetTolerancesScale()};
        Desc.gravity = PhysicsRuntime::ToPxVec3(Settings.mGravity);
        Desc.cpuDispatcher = Runtime.GetDispatcher();
        Desc.filterShader = SimulationFilterShader;
        Desc.simulationEventCallback = this;
        Desc.filterCallback = this;
        Desc.flags |= physx::PxSceneFlag::eENABLE_ACTIVE_ACTORS;

        if (Settings.mEnableCCD) {
            Desc.flags |= physx::PxSceneFlag::eENABLE_CCD;
        }

        if (Settings.mEnablePCM) {
            Desc.flags |= physx::PxSceneFlag::eENABLE_PCM;
        }

        if (!Desc.isValid()) {
            MACAW_LOG(LogPhysics, Error, "Cannot create a physics scene from an invalid scene description.");

            return false;
        }

        mScene.reset(Runtime.GetPhysics()->createScene(Desc));

        if (mScene == nullptr) {
            MACAW_LOG(LogPhysics, Error, "Cannot create the PhysX scene.");

            return false;
        }

        if (physx::PxPvdSceneClient* PvdClient{mScene->getScenePvdClient()}) {
            PvdClient->setScenePvdFlag(physx::PxPvdSceneFlag::eTRANSMIT_CONSTRAINTS, true);
            PvdClient->setScenePvdFlag(physx::PxPvdSceneFlag::eTRANSMIT_CONTACTS, true);
            PvdClient->setScenePvdFlag(physx::PxPvdSceneFlag::eTRANSMIT_SCENEQUERIES, true);
        }

        mVehicleManager = std::make_unique<PhysicsRuntime::FVehicleManager>(*mScene, Runtime);
        mRuntime = &Runtime;

        return true;
    }

    /**
     * @brief PhysX 장면과 보관한 시뮬레이션 결과를 해제합니다.
     */
    void PhysicsRuntime::FPhysicsScene::Shutdown() {
        mContacts.clear();
        mDisabledPairs.clear();
        mVehicleManager.reset();
        mScene.reset();
        mRuntime = nullptr;
    }

    /**
     * @brief 장면을 만들 때 사용한 PhysX 런타임을 반환합니다.
     */
    PhysicsRuntime::FPhysicsRuntime* PhysicsRuntime::FPhysicsScene::GetRuntime() const {
        return mRuntime;
    }

    PhysicsRuntime::FVehicleManager* PhysicsRuntime::FPhysicsScene::GetVehicleManager() const {
        return mVehicleManager.get();
    }

    /**
     * @brief PhysX 장면이 준비되었는지 반환합니다.
     */
    bool PhysicsRuntime::FPhysicsScene::IsInitialized() const {
        return mScene != nullptr;
    }

    /**
     * @brief 장면을 한 스텝 진행하고 결과를 받을 때까지 기다립니다.
     */
    bool PhysicsRuntime::FPhysicsScene::Simulate(Core::FTime Step) {
        if (mScene == nullptr || !Step.IsFinite() || Step <= Core::FTime{}) {
            return false;
        }

        {
            const Core::FScopedStatTimer StatTimer{PhysicsRuntime::PhysicsStats::Simulate};

            mContacts.clear();
            mVehicleManager->Step(Step);
            mScene->simulate(Step.GetBase());

            const Core::FScopedStatTimer FetchTimer{PhysicsRuntime::PhysicsStats::FetchResults};
            mScene->fetchResults(true);
        }

        physx::PxSimulationStatistics SimulationStats{};
        mScene->getSimulationStatistics(SimulationStats);

        PhysicsRuntime::PhysicsStats::Steps.Add();
        PhysicsRuntime::PhysicsStats::Contacts.Add(static_cast<double>(mContacts.size()));
        PhysicsRuntime::PhysicsStats::ActiveDynamicBodies.Add(static_cast<double>(SimulationStats.nbActiveDynamicBodies));
        PhysicsRuntime::PhysicsStats::ActiveKinematicBodies.Add(static_cast<double>(SimulationStats.nbActiveKinematicBodies));
        PhysicsRuntime::PhysicsStats::ActiveConstraints.Add(static_cast<double>(SimulationStats.nbActiveConstraints));
        PhysicsRuntime::PhysicsStats::DiscreteContactPairs.Add(static_cast<double>(SimulationStats.nbDiscreteContactPairsTotal));
        PhysicsRuntime::PhysicsStats::DynamicBodies.Set(static_cast<double>(mScene->getNbActors(physx::PxActorTypeFlag::eRIGID_DYNAMIC)));
        PhysicsRuntime::PhysicsStats::StaticBodies.Set(static_cast<double>(mScene->getNbActors(physx::PxActorTypeFlag::eRIGID_STATIC)));

        return true;
    }

    /**
     * @brief PhysX 장면의 비소유 주소를 반환합니다.
     */
    physx::PxScene* PhysicsRuntime::FPhysicsScene::GetScene() const {
        return mScene.get();
    }

    /**
     * @brief 같은 물리 에셋 인스턴스의 바디를 묶는 새 충돌 그룹 번호를 만듭니다.
     */
    Uint32 PhysicsRuntime::FPhysicsScene::AllocateCollisionGroup() {
        return ++mLastCollisionGroup;
    }

    /**
     * @brief 충돌 그룹 안에서 서로 충돌하지 않을 바디 쌍을 등록합니다.
     */
    void PhysicsRuntime::FPhysicsScene::SetCollisionDisabledPairs(Uint32 GroupId, const Core::TArray<std::pair<Uint32, Uint32>>& Pairs) {
        std::unordered_set<Uint64>& Set{mDisabledPairs[GroupId]};

        Set.clear();

        for (const auto& Pair : Pairs) {
            Set.insert(MakePairKey(Pair.first, Pair.second));
        }
    }

    /**
     * @brief 같은 충돌 그룹의 바디 쌍이 비활성으로 등록되어 있으면 그 쌍을 충돌에서 제외합니다.
     */
    physx::PxFilterFlags PhysicsRuntime::FPhysicsScene::pairFound(physx::PxU32, physx::PxFilterObjectAttributes, physx::PxFilterData FilterData0, const physx::PxActor*, const physx::PxShape*, physx::PxFilterObjectAttributes, physx::PxFilterData FilterData1, const physx::PxActor*, const physx::PxShape*, physx::PxPairFlags&) {
        const auto Found{mDisabledPairs.find(FilterData0.word1)};

        if (Found != mDisabledPairs.end() && Found->second.contains(MakePairKey(FilterData0.word2, FilterData1.word2))) {
            return physx::PxFilterFlag::eKILL;
        }

        // 셰이더가 정한 접촉 플래그를 그대로 쓴다.
        return physx::PxFilterFlag::eDEFAULT;
    }

    /**
     * @brief 쌍이 사라져도 보관한 상태가 없으므로 아무것도 하지 않습니다.
     */
    void PhysicsRuntime::FPhysicsScene::pairLost(physx::PxU32, physx::PxFilterObjectAttributes, physx::PxFilterData, physx::PxFilterObjectAttributes, physx::PxFilterData, bool) {
    }

    /**
     * @brief 추적 중인 쌍의 상태를 바꾸지 않습니다.
     */
    bool PhysicsRuntime::FPhysicsScene::statusChange(physx::PxU32&, physx::PxPairFlags&, physx::PxFilterFlags&) {
        return false;
    }

    /**
     * @brief 마지막 스텝에서 자세가 바뀐 강체 목록을 반환합니다.
     */
    std::span<physx::PxActor* const> PhysicsRuntime::FPhysicsScene::GetActiveActors() const {
        if (mScene == nullptr) {
            return {};
        }

        physx::PxU32 Count{};
        physx::PxActor** Actors{mScene->getActiveActors(Count)};

        return {Actors, Count};
    }

    /**
     * @brief 마지막 스텝에서 새로 시작된 접촉 목록을 반환합니다.
     */
    const Core::TArray<PhysicsRuntime::FPhysicsContact>& PhysicsRuntime::FPhysicsScene::GetContacts() const {
        return mContacts;
    }

    /**
     * @brief 새로 시작된 접촉을 접촉 목록에 기록합니다.
     * @details 형상 쌍마다 접촉점 위치를 평균하고 충격량을 합산하며, 법선은 첫 접촉점의 값을 사용합니다.
     */
    void PhysicsRuntime::FPhysicsScene::onContact(const physx::PxContactPairHeader& PairHeader, const physx::PxContactPair* Pairs, physx::PxU32 PairCount) {
        if (PairHeader.flags.isSet(physx::PxContactPairHeaderFlag::eREMOVED_ACTOR_0) || PairHeader.flags.isSet(physx::PxContactPairHeaderFlag::eREMOVED_ACTOR_1)) {
            return;
        }

        for (physx::PxU32 PairIndex{}; PairIndex < PairCount; ++PairIndex) {
            const physx::PxContactPair& Pair{Pairs[PairIndex]};

            if (!Pair.events.isSet(physx::PxPairFlag::eNOTIFY_TOUCH_FOUND)) {
                continue;
            }

            physx::PxContactPairPoint Points[MaxContactPointsPerPair]{};
            const physx::PxU32 PointCount{Pair.extractContacts(Points, MaxContactPointsPerPair)};

            PhysicsRuntime::FPhysicsContact Contact{};
            Contact.mActor0 = PairHeader.actors[0];
            Contact.mActor1 = PairHeader.actors[1];

            if (PointCount > 0) {
                physx::PxVec3 PositionSum{physx::PxZero};
                physx::PxVec3 ImpulseSum{physx::PxZero};

                for (physx::PxU32 PointIndex{}; PointIndex < PointCount; ++PointIndex) {
                    PositionSum += Points[PointIndex].position;
                    ImpulseSum += Points[PointIndex].impulse;
                }

                Contact.mPoint = PhysicsRuntime::ToFVector(PositionSum / static_cast<physx::PxReal>(PointCount));
                Contact.mNormal = PhysicsRuntime::ToFVector(Points[0].normal);
                Contact.mImpulse = PhysicsRuntime::ToFVector(ImpulseSum);
            }

            mContacts.push_back(Contact);
        }
    }

    /**
     * @brief 트리거 이벤트는 아직 사용하지 않습니다.
     */
    void PhysicsRuntime::FPhysicsScene::onTrigger(physx::PxTriggerPair*, physx::PxU32) {
    }

    /**
     * @brief 조인트 파괴 이벤트는 아직 사용하지 않습니다.
     */
    void PhysicsRuntime::FPhysicsScene::onConstraintBreak(physx::PxConstraintInfo*, physx::PxU32) {
    }

    /**
     * @brief 강체 깨어남 이벤트는 아직 사용하지 않습니다.
     */
    void PhysicsRuntime::FPhysicsScene::onWake(physx::PxActor**, physx::PxU32) {
    }

    /**
     * @brief 강체 잠듦 이벤트는 아직 사용하지 않습니다.
     */
    void PhysicsRuntime::FPhysicsScene::onSleep(physx::PxActor**, physx::PxU32) {
    }

    /**
     * @brief 시뮬레이션 도중 자세 전달 이벤트는 아직 사용하지 않습니다.
     */
    void PhysicsRuntime::FPhysicsScene::onAdvance(const physx::PxRigidBody* const*, const physx::PxTransform*, const physx::PxU32) {
    }

    namespace {

        // 구 쓸기에서 Block 응답 형상만 받고 제외 조건에 맞는 바디는 거릅니다.
        class FSphereSweepFilter final : public physx::PxQueryFilterCallback {
        public:
            explicit FSphereSweepFilter(const PhysicsRuntime::FIgnoreBodyPredicate& ShouldIgnore)
                : mShouldIgnore{ShouldIgnore} {
            }

            physx::PxQueryHitType::Enum preFilter(const physx::PxFilterData&, const physx::PxShape* Shape, const physx::PxRigidActor* Actor, physx::PxHitFlags&) override {
                if (Shape == nullptr || Actor == nullptr) {
                    return physx::PxQueryHitType::eNONE;
                }

                const PhysicsRuntime::FBodyInstance* Body{PhysicsRuntime::FBodyInstance::FromPxActor(*Actor)};

                if (Body == nullptr || (mShouldIgnore && mShouldIgnore(*Body))) {
                    return physx::PxQueryHitType::eNONE;
                }

                // 시작부터 겹친 형상을 직접 거르려고 막힘도 터치로 받아 가장 가까운 것을 고릅니다.
                return static_cast<PhysicsRuntime::ECollisionResponse>(Shape->getQueryFilterData().word0) == PhysicsRuntime::ECollisionResponse::Block ? physx::PxQueryHitType::eTOUCH : physx::PxQueryHitType::eNONE;
            }

            physx::PxQueryHitType::Enum postFilter(const physx::PxFilterData&, const physx::PxQueryHit&) override {
                return physx::PxQueryHitType::eNONE;
            }

        private:
            const PhysicsRuntime::FIgnoreBodyPredicate& mShouldIgnore;
        };

    }

    bool PhysicsRuntime::FPhysicsScene::SweepSphere(const FVector& Start, const FVector& Delta, Core::FLength Radius, PhysicsRuntime::FBodySweepHit& OutHit, const PhysicsRuntime::FIgnoreBodyPredicate& ShouldIgnore) const {
        if (mScene == nullptr || !Start.IsFinite() || !Delta.IsFinite() || !Radius.IsFinite()) {
            return false;
        }

        const float Length{Delta.Length()};

        if (Length <= 1.0e-6f) {
            return false;
        }

        const Core::FScopedStatTimer StatTimer{PhysicsRuntime::PhysicsStats::SweepQueryTime};
        PhysicsRuntime::PhysicsStats::SweepQueries.Add();

        FSphereSweepFilter Callback{ShouldIgnore};
        const physx::PxQueryFilterData Filter{physx::PxQueryFlag::eSTATIC | physx::PxQueryFlag::eDYNAMIC | physx::PxQueryFlag::ePREFILTER | physx::PxQueryFlag::eNO_BLOCK};
        const physx::PxTransform Pose{PhysicsRuntime::ToPxVec3(Start)};
        const physx::PxVec3 Direction{PhysicsRuntime::ToPxVec3(Delta / Length)};
        Core::TArray<physx::PxSweepHit> Touches(64);
        physx::PxSweepBuffer Buffer{Touches.data(), static_cast<physx::PxU32>(Touches.size())};

        // 반지름이 0인 구는 PhysX가 받지 않으므로 아주 작은 값으로 올립니다.
        const physx::PxSphereGeometry Sphere{std::max(Radius, Core::FLength::FromBase(0.0001f)).GetBase()};

        if (!mScene->sweep(Sphere, Pose, Direction, Length, Buffer, physx::PxHitFlag::eDEFAULT, Filter, &Callback)) {
            return false;
        }

        bool Found{};

        for (physx::PxU32 Index{}; Index < Buffer.getNbTouches(); ++Index) {
            const physx::PxSweepHit& Touch{Buffer.getTouch(Index)};

            const Core::FLength Distance{Core::FLength::FromBase(Touch.distance)};

            if (Touch.hadInitialOverlap() || (Found && Distance >= OutHit.mDistance)) {
                continue;
            }

            Found = true;
            OutHit.mBody = Touch.actor != nullptr ? PhysicsRuntime::FBodyInstance::FromPxActor(*Touch.actor) : nullptr;
            OutHit.mDistance = Distance;
            OutHit.mImpactPoint = PhysicsRuntime::ToFVector(Touch.position);
            OutHit.mImpactNormal = PhysicsRuntime::ToFVector(Touch.normal);
            OutHit.mPenetrationDepth = {};
            OutHit.mStartPenetrating = false;
        }

        return Found;
    }

}
