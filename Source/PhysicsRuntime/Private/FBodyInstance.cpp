/**
 * @file FBodyInstance.cpp
 * @brief 컴포넌트 하나에 대응하는 PhysX 강체와 그 시뮬레이션 설정을 관리합니다.
 * @ingroup PhysicsRuntime
 */

#include "Core/Types.h"
#include "Core/Literals/UnitLiterals.h"
#include "PhysicsRuntime/FBodyInstance.h"
#include "PhysicsRuntime/FPhysicsRuntime.h"
#include "PhysicsRuntime/FPhysicsScene.h"
#include "PhysicsRuntime/FVehicleManager.h"
#include "PhysicsRuntime/PhysicsConversion.h"
#include "PhysicsRuntime/PhysicsStats.h"
#include "PhysicsRuntime/UBodySetup.h"
#include "Core/Log/Log.h"

#include <PxPhysicsAPI.h>

#include <algorithm>
#include <cmath>

using namespace Core::Literals;

namespace PhysicsRuntime {

    /**
     * @brief 바디 생성과 설정 검증에 쓰는 로그 범주와 보조 함수입니다.
     */
    namespace {
        DEFINE_LOG_CATEGORY_STATIC(LogPhysics, Log, All);

        /**
         * @brief 질량과 감쇠 계수로 쓸 수 있는 값인지 검사합니다.
         * @param[in] Value 검사할 값입니다.
         * @return 0 이상의 유한값이면 true입니다.
         */
        bool IsNonNegativeFinite(float Value) {
            return std::isfinite(Value) && Value >= 0.0f;
        }

        void UnregisterVehicle(FPhysicsScene* Scene, const FBodyInstance& Body) {
            if (auto* Vehicles{Scene != nullptr ? Scene->GetVehicleManager() : nullptr}) {
                Vehicles->UnregisterVehicle(Body);
            }
        }

        /**
         * @brief 프리셋이 정하는 참여 방식과 응답을 구합니다.
         * @param[in] Preset 조회할 프리셋입니다.
         * @param[out] CollisionEnabled 프리셋의 참여 방식입니다.
         * @param[out] Response 프리셋의 응답입니다.
         * @return Custom이 아니어서 값을 채웠으면 true입니다.
         */
        bool GetPresetSettings(PhysicsRuntime::ECollisionPreset Preset, PhysicsRuntime::ECollisionEnabled& CollisionEnabled, PhysicsRuntime::ECollisionResponse& Response) {
            switch (Preset) {
            case PhysicsRuntime::ECollisionPreset::NoCollision:
                CollisionEnabled = PhysicsRuntime::ECollisionEnabled::NoCollision;
                Response = PhysicsRuntime::ECollisionResponse::Ignore;
                return true;
            case PhysicsRuntime::ECollisionPreset::BlockAll:
                CollisionEnabled = PhysicsRuntime::ECollisionEnabled::QueryAndPhysics;
                Response = PhysicsRuntime::ECollisionResponse::Block;
                return true;
            case PhysicsRuntime::ECollisionPreset::OverlapAll:
                CollisionEnabled = PhysicsRuntime::ECollisionEnabled::QueryOnly;
                Response = PhysicsRuntime::ECollisionResponse::Overlap;
                return true;
            default:
                return false;
            }
        }

        /**
         * @brief 쿼리하는 바디와 대상 형상의 응답을 합쳐 쿼리 결과의 종류를 정하는 필터입니다.
         * @details 대상 형상의 쿼리 필터 데이터 word0에 담긴 응답을 읽습니다. 쿼리하는 바디 자신은 항상 제외합니다.
         */
        class FResponseQueryFilter final : public physx::PxQueryFilterCallback {
        public:
            /**
             * @brief 응답별 결과 종류와 제외 조건을 받아 필터를 구성합니다.
             * @param[in] Querier 쿼리하는 바디입니다.
             * @param[in] BlockHit 합친 응답이 Block일 때의 결과 종류입니다.
             * @param[in] OverlapHit 합친 응답이 Overlap일 때의 결과 종류입니다.
             * @param[in] ShouldIgnore true를 반환한 바디를 제외하며, 비어 있으면 쓰지 않습니다.
             */
            FResponseQueryFilter(const PhysicsRuntime::FBodyInstance& Querier, physx::PxQueryHitType::Enum BlockHit, physx::PxQueryHitType::Enum OverlapHit, const PhysicsRuntime::FIgnoreBodyPredicate& ShouldIgnore)
                : mQuerier{Querier}
                , mBlockHit{BlockHit}
                , mOverlapHit{OverlapHit}
                , mShouldIgnore{ShouldIgnore} {
            }

            /**
             * @brief 대상 형상의 응답과 제외 조건으로 결과 종류를 정합니다.
             */
            physx::PxQueryHitType::Enum preFilter(const physx::PxFilterData&, const physx::PxShape* Shape, const physx::PxRigidActor* Actor, physx::PxHitFlags&) override {
                if (Shape == nullptr || Actor == nullptr || Actor == mQuerier.GetPxRigidActor()) {
                    return physx::PxQueryHitType::eNONE;
                }

                const PhysicsRuntime::FBodyInstance* Other{PhysicsRuntime::FBodyInstance::FromPxActor(*Actor)};

                if (Other == nullptr || (mShouldIgnore && mShouldIgnore(*Other))) {
                    return physx::PxQueryHitType::eNONE;
                }

                const auto OtherResponse{static_cast<PhysicsRuntime::ECollisionResponse>(Shape->getQueryFilterData().word0)};

                switch (PhysicsRuntime::CombineCollisionResponse(mQuerier.GetCollisionResponse(), OtherResponse)) {
                case PhysicsRuntime::ECollisionResponse::Block:
                    return mBlockHit;
                case PhysicsRuntime::ECollisionResponse::Overlap:
                    return mOverlapHit;
                default:
                    return physx::PxQueryHitType::eNONE;
                }
            }

            /**
             * @brief 사후 필터는 쓰지 않습니다.
             */
            physx::PxQueryHitType::Enum postFilter(const physx::PxFilterData&, const physx::PxQueryHit&) override {
                return physx::PxQueryHitType::eNONE;
            }

        private:
            /**
             * @brief 쿼리하는 바디입니다.
             */
            const PhysicsRuntime::FBodyInstance& mQuerier;

            /**
             * @brief 합친 응답이 Block일 때의 결과 종류입니다.
             */
            physx::PxQueryHitType::Enum mBlockHit;

            /**
             * @brief 합친 응답이 Overlap일 때의 결과 종류입니다.
             */
            physx::PxQueryHitType::Enum mOverlapHit;

            /**
             * @brief 제외할 바디를 고르는 판정 함수입니다.
             */
            const PhysicsRuntime::FIgnoreBodyPredicate& mShouldIgnore;
        };
    }

    /**
     * @brief 실제 바디가 없는 기본 설정의 바디 인스턴스를 구성합니다.
     */
    PhysicsRuntime::FBodyInstance::FBodyInstance()
		: mLinearDamping{0.01f / 1_s},
		  mAngularDamping{0.05f / 1_s} {
    }

    /**
     * @brief 다른 바디 인스턴스의 설정만 복사합니다.
     */
    PhysicsRuntime::FBodyInstance::FBodyInstance(const FBodyInstance& Other)
        : mSimulatePhysics{Other.mSimulatePhysics}
        , mEnableGravity{Other.mEnableGravity}
        , mUseCCD{Other.mUseCCD}
        , mMassInKg{Other.mMassInKg}
        , mLinearDamping{Other.mLinearDamping}
        , mAngularDamping{Other.mAngularDamping}
        , mInertiaScale{Other.mInertiaScale}
        , mOverridePhysicalMaterial{Other.mOverridePhysicalMaterial}
        , mStaticFriction{Other.mStaticFriction}
        , mDynamicFriction{Other.mDynamicFriction}
        , mRestitution{Other.mRestitution}
        , mCollisionPreset{Other.mCollisionPreset}
        , mCollisionEnabled{Other.mCollisionEnabled}
        , mCollisionResponse{Other.mCollisionResponse} {
    }

    /**
     * @brief 이 객체의 실제 바디를 해제하고 다른 바디 인스턴스의 설정만 복사합니다.
     */
    PhysicsRuntime::FBodyInstance& PhysicsRuntime::FBodyInstance::operator=(const FBodyInstance& Other) {
        if (this == &Other) {
            return *this;
        }

        TermBody();

        mSimulatePhysics = Other.mSimulatePhysics;
        mEnableGravity = Other.mEnableGravity;
        mUseCCD = Other.mUseCCD;
        mMassInKg = Other.mMassInKg;
        mLinearDamping = Other.mLinearDamping;
        mAngularDamping = Other.mAngularDamping;
        mInertiaScale = Other.mInertiaScale;
        mOverridePhysicalMaterial = Other.mOverridePhysicalMaterial;
        mStaticFriction = Other.mStaticFriction;
        mDynamicFriction = Other.mDynamicFriction;
        mRestitution = Other.mRestitution;
        mCollisionPreset = Other.mCollisionPreset;
        mCollisionEnabled = Other.mCollisionEnabled;
        mCollisionResponse = Other.mCollisionResponse;

        return *this;
    }

    /**
     * @brief 실제 바디를 해제합니다.
     */
    PhysicsRuntime::FBodyInstance::~FBodyInstance() {
        TermBody();
    }

    /**
     * @brief 바디 셋업의 충돌 형상으로 PhysX 강체를 만들어 장면에 추가합니다.
     */
    bool PhysicsRuntime::FBodyInstance::InitBody(const PhysicsRuntime::UBodySetup& Setup, const FTransform& Transform, Engine::UPrimitiveComponent* OwnerComponent, PhysicsRuntime::FPhysicsScene& Scene) {
        return InitBody(Setup, Transform, OwnerComponent, Scene, PhysicsRuntime::FInitBodySpawnParams{!mSimulatePhysics});
    }

    /**
     * @brief 강체 종류를 직접 정해 바디 셋업의 충돌 형상으로 PhysX 강체를 만들어 장면에 추가합니다.
     */
    bool PhysicsRuntime::FBodyInstance::InitBody(const PhysicsRuntime::UBodySetup& Setup, const FTransform& Transform, Engine::UPrimitiveComponent* OwnerComponent, PhysicsRuntime::FPhysicsScene& Scene, const PhysicsRuntime::FInitBodySpawnParams& SpawnParams) {
        TermBody();

        PhysicsRuntime::FPhysicsRuntime* Runtime{Scene.GetRuntime()};

        if (!Scene.IsInitialized() || Runtime == nullptr) {
            MACAW_LOG(LogPhysics, Warning, "Cannot create a body for '{}' without an initialized physics scene.", Setup.GetName());

            return false;
        }

        if (Setup.GetAggGeom().IsEmpty()) {
            return false;
        }

        const physx::PxTransform Pose{PhysicsRuntime::ToPxTransform(Transform)};

        if (!Pose.isValid()) {
            MACAW_LOG(LogPhysics, Warning, "Cannot create a body for '{}' from an invalid transform.", Setup.GetName());

            return false;
        }

        physx::PxPhysics& Physics{*Runtime->GetPhysics()};
        std::unique_ptr<physx::PxRigidActor, PhysicsRuntime::FPhysXRelease> Actor{};

        if (SpawnParams.mStaticPhysics) {
            Actor.reset(Physics.createRigidStatic(Pose));
        } else if (physx::PxRigidDynamic* Dynamic{Physics.createRigidDynamic(Pose)}) {
            // 시뮬레이션하지 않는 움직이는 강체는 코드가 옮기는 위치로만 갑니다.
            Dynamic->setRigidBodyFlag(physx::PxRigidBodyFlag::eKINEMATIC, !mSimulatePhysics);
            Actor.reset(Dynamic);
        }

        if (Actor == nullptr) {
            MACAW_LOG(LogPhysics, Error, "Cannot create a PhysX rigid actor for '{}'.", Setup.GetName());

            return false;
        }

        // 컴포넌트가 재질을 지정했으면 그 값으로 이 바디만의 재질을 만든다. 형상이 재질을 참조하므로 이 함수가 끝나 해제해도 형상이 있는 동안은 유지된다.
        std::unique_ptr<physx::PxMaterial, PhysicsRuntime::FPhysXRelease> OverrideMaterial{};

        if (mOverridePhysicalMaterial) {
            const bool Finite{std::isfinite(mStaticFriction) && std::isfinite(mDynamicFriction) && std::isfinite(mRestitution)};

            if (Finite) {
                OverrideMaterial.reset(Physics.createMaterial(std::max(mStaticFriction, 0.0f), std::max(mDynamicFriction, 0.0f), std::clamp(mRestitution, 0.0f, 1.0f)));
            }

            if (OverrideMaterial == nullptr) {
                MACAW_LOG(LogPhysics, Warning, "Cannot create the physical material override for '{}'. Using the default material.", Setup.GetName());
            }
        }

        if (Setup.AddShapesToRigidActor(*Actor, Transform.mScale, *Runtime, OverrideMaterial.get()) == 0) {
            MACAW_LOG(LogPhysics, Warning, "Body setup '{}' has no valid collision shape at scale ({}, {}, {}).", Setup.GetName(), Transform.mScale.x, Transform.mScale.y, Transform.mScale.z);

            return false;
        }

        Actor->userData = this;
        mActor = std::move(Actor);
        mScene = &Scene;
        mOwnerComponent = OwnerComponent;
        mBodyScale = Transform.mScale;

        ApplyCollisionSettings();

        if (physx::PxRigidDynamic* Dynamic{GetPxRigidDynamic()}) {
            ApplyDynamicSettings(*Dynamic);
        }

        Scene.GetScene()->addActor(*mActor);

        return true;
    }

    /**
     * @brief 실제 바디를 장면에서 제거하고 해제합니다.
     */
    void PhysicsRuntime::FBodyInstance::TermBody() {
        UnregisterVehicle(mScene, *this);
        mActor.reset();
        mScene = nullptr;
        mOwnerComponent = nullptr;
    }

    /**
     * @brief 실제 바디가 있는지 반환합니다.
     */
    bool PhysicsRuntime::FBodyInstance::IsValidBodyInstance() const {
        return mActor != nullptr;
    }

    /**
     * @brief 실제 바디가 시뮬레이션되는 동적 강체인지 반환합니다.
     */
    bool PhysicsRuntime::FBodyInstance::IsDynamic() const {
        return GetPxRigidDynamic() != nullptr;
    }

    /**
     * @brief 바디를 소유하는 컴포넌트를 반환합니다.
     */
    Engine::UPrimitiveComponent* PhysicsRuntime::FBodyInstance::GetOwnerComponent() const {
        return mOwnerComponent;
    }

    /**
     * @brief 이 바디의 모든 PhysX 형상에 충돌 필터 데이터를 설정합니다.
     */
    void PhysicsRuntime::FBodyInstance::SetCollisionFilter(Uint32 GroupId, Uint32 BodyIndex, bool CollisionEnabled) {
        physx::PxRigidActor* Actor{GetPxRigidActor()};

        if (Actor == nullptr) {
            return;
        }

        const physx::PxU32 Count{Actor->getNbShapes()};
        Core::TArray<physx::PxShape*> Shapes(Count);

        Actor->getShapes(Shapes.data(), Count);

        // word0은 충돌 응답이므로 건드리지 않는다. word1: 충돌 그룹, word2: 그룹 안의 바디 번호, word3: 1이면 충돌 끔
        for (physx::PxShape* Shape : Shapes) {
            physx::PxFilterData Data{Shape->getSimulationFilterData()};

            Data.word1 = GroupId;
            Data.word2 = BodyIndex;
            Data.word3 = CollisionEnabled ? 0u : 1u;
            Shape->setSimulationFilterData(Data);
        }
    }

    /**
     * @brief PhysX 강체를 반환합니다.
     */
    physx::PxRigidActor* PhysicsRuntime::FBodyInstance::GetPxRigidActor() const {
        return mActor.get();
    }

    /**
     * @brief 동적 PhysX 강체를 반환합니다.
     */
    physx::PxRigidDynamic* PhysicsRuntime::FBodyInstance::GetPxRigidDynamic() const {
        return mActor != nullptr ? mActor->is<physx::PxRigidDynamic>() : nullptr;
    }

    /**
     * @brief 바디의 현재 월드 트랜스폼을 반환합니다.
     */
    FTransform PhysicsRuntime::FBodyInstance::GetBodyTransform() const {
        if (mActor == nullptr) {
            return FTransform{};
        }

        return PhysicsRuntime::ToFTransform(mActor->getGlobalPose(), mBodyScale);
    }

    /**
     * @brief 형상 크기에 반영한 스케일을 반환합니다.
     */
    const FVector& PhysicsRuntime::FBodyInstance::GetBodyScale() const {
        return mBodyScale;
    }

    /**
     * @brief 바디를 지정한 위치·회전으로 순간 이동합니다.
     */
    bool PhysicsRuntime::FBodyInstance::SetBodyTransform(const FTransform& Transform) {
        if (mActor == nullptr) {
            return false;
        }

        const physx::PxTransform Pose{PhysicsRuntime::ToPxTransform(Transform)};

        if (!Pose.isValid()) {
            return false;
        }

        physx::PxRigidDynamic* Dynamic{GetPxRigidDynamic()};

        // 키네마틱 강체는 목표 위치를 주어야 다음 스텝에서 닿은 물체를 밀어냅니다.
        if (Dynamic != nullptr && Dynamic->getScene() != nullptr && Dynamic->getRigidBodyFlags().isSet(physx::PxRigidBodyFlag::eKINEMATIC)) {
            Dynamic->setKinematicTarget(Pose);
        } else {
            mActor->setGlobalPose(Pose);
        }

        return true;
    }

    /**
     * @brief 동적 강체의 질량 중심에 힘을 더합니다.
     */
    bool PhysicsRuntime::FBodyInstance::AddForce(const FVector& Force, bool AccelChange) {
        physx::PxRigidDynamic* Dynamic{GetPxRigidDynamic()};
        const physx::PxVec3 Value{PhysicsRuntime::ToPxVec3(Force)};

        if (Dynamic == nullptr || !Value.isFinite()) {
            return false;
        }

        Dynamic->addForce(Value, AccelChange ? physx::PxForceMode::eACCELERATION : physx::PxForceMode::eFORCE);

        return true;
    }

    /**
     * @brief 동적 강체의 질량 중심에 충격량을 더합니다.
     */
    bool PhysicsRuntime::FBodyInstance::AddImpulse(const FVector& Impulse, bool VelocityChange) {
        physx::PxRigidDynamic* Dynamic{GetPxRigidDynamic()};
        const physx::PxVec3 Value{PhysicsRuntime::ToPxVec3(Impulse)};

        if (Dynamic == nullptr || !Value.isFinite()) {
            return false;
        }

        Dynamic->addForce(Value, VelocityChange ? physx::PxForceMode::eVELOCITY_CHANGE : physx::PxForceMode::eIMPULSE);

        return true;
    }

    bool PhysicsRuntime::FBodyInstance::AddImpulseAtLocation(const FVector& Impulse, const FVector& Location) {
        physx::PxRigidDynamic* Dynamic{GetPxRigidDynamic()};
        const physx::PxVec3 Value{PhysicsRuntime::ToPxVec3(Impulse)};
        const physx::PxVec3 Position{PhysicsRuntime::ToPxVec3(Location)};

        if (Dynamic == nullptr || Dynamic->getRigidBodyFlags().isSet(physx::PxRigidBodyFlag::eKINEMATIC) || !Value.isFinite() || !Position.isFinite()) {
            return false;
        }

        physx::PxRigidBodyExt::addForceAtPos(*Dynamic, Value, Position, physx::PxForceMode::eIMPULSE);
        return true;
    }

    /**
     * @brief 동적 강체의 선속도를 반환합니다.
     */
    FVector PhysicsRuntime::FBodyInstance::GetLinearVelocity() const {
        const physx::PxRigidDynamic* Dynamic{GetPxRigidDynamic()};

        return Dynamic != nullptr ? PhysicsRuntime::ToFVector(Dynamic->getLinearVelocity()) : FVector{};
    }

    /**
     * @brief 동적 강체의 선속도를 설정합니다.
     */
    bool PhysicsRuntime::FBodyInstance::SetLinearVelocity(const FVector& Velocity) {
        physx::PxRigidDynamic* Dynamic{GetPxRigidDynamic()};
        const physx::PxVec3 Value{PhysicsRuntime::ToPxVec3(Velocity)};

        if (Dynamic == nullptr || !Value.isFinite()) {
            return false;
        }

        Dynamic->setLinearVelocity(Value);

        return true;
    }

    /**
     * @brief 동적 강체의 각속도를 반환합니다.
     */
    FVector PhysicsRuntime::FBodyInstance::GetAngularVelocity() const {
        const physx::PxRigidDynamic* Dynamic{GetPxRigidDynamic()};

        return Dynamic != nullptr ? PhysicsRuntime::ToFVector(Dynamic->getAngularVelocity()) : FVector{};
    }

    /**
     * @brief 동적 강체의 각속도를 설정합니다.
     */
    bool PhysicsRuntime::FBodyInstance::SetAngularVelocity(const FVector& Velocity) {
        physx::PxRigidDynamic* Dynamic{GetPxRigidDynamic()};
        const physx::PxVec3 Value{PhysicsRuntime::ToPxVec3(Velocity)};

        if (Dynamic == nullptr || !Value.isFinite()) {
            return false;
        }

        Dynamic->setAngularVelocity(Value);

        return true;
    }

    /**
     * @brief PhysX 강체에서 그 강체를 만든 바디 인스턴스를 찾습니다.
     */
    PhysicsRuntime::FBodyInstance* PhysicsRuntime::FBodyInstance::FromPxActor(const physx::PxActor& Actor) {
        return static_cast<FBodyInstance*>(Actor.userData);
    }

    /**
     * @brief 바디의 현재 자세에서 각 형상과 겹치는 다른 바디를 장면 쿼리로 찾습니다.
     */
    bool PhysicsRuntime::FBodyInstance::OverlapMulti(Core::TArray<FBodyInstance*>& OutOverlaps) const {
        OutOverlaps.clear();

        if (mActor == nullptr || mScene == nullptr || mScene->GetScene() == nullptr || !IsQueryCollisionEnabled()) {
            return false;
        }

        const Core::FScopedStatTimer StatTimer{PhysicsRuntime::PhysicsStats::OverlapQueryTime};
        PhysicsRuntime::PhysicsStats::OverlapQueries.Add();

        Core::TArray<physx::PxShape*> Shapes(mActor->getNbShapes());
        mActor->getShapes(Shapes.data(), static_cast<physx::PxU32>(Shapes.size()));

        const physx::PxTransform ActorPose{mActor->getGlobalPose()};
        // 합친 응답이 Overlap인 강체만 터치로 받고, Block은 겹침이 아니라 막힘이므로 뺍니다.
        const PhysicsRuntime::FIgnoreBodyPredicate NoIgnore{};
        FResponseQueryFilter Callback{*this, physx::PxQueryHitType::eNONE, physx::PxQueryHitType::eTOUCH, NoIgnore};
        const physx::PxQueryFilterData Filter{physx::PxQueryFlag::eSTATIC | physx::PxQueryFlag::eDYNAMIC | physx::PxQueryFlag::ePREFILTER | physx::PxQueryFlag::eNO_BLOCK};
        Core::TArray<physx::PxOverlapHit> Touches(256);

        for (const physx::PxShape* Shape : Shapes) {
            if (!Shape->getFlags().isSet(physx::PxShapeFlag::eSCENE_QUERY_SHAPE)) {
                continue;
            }

            const physx::PxGeometryHolder Geometry{Shape->getGeometry()};
            physx::PxOverlapBuffer Buffer{Touches.data(), static_cast<physx::PxU32>(Touches.size())};

            if (!mScene->GetScene()->overlap(Geometry.any(), ActorPose * Shape->getLocalPose(), Buffer, Filter, &Callback)) {
                continue;
            }

            for (physx::PxU32 Index{}; Index < Buffer.getNbTouches(); ++Index) {
                const physx::PxRigidActor* OtherActor{Buffer.getTouch(Index).actor};

                if (OtherActor == nullptr || OtherActor == mActor.get()) {
                    continue;
                }

                if (FBodyInstance* Other{FromPxActor(*OtherActor)}; Other != nullptr && std::find(OutOverlaps.begin(), OutOverlaps.end(), Other) == OutOverlaps.end()) {
                    OutOverlaps.push_back(Other);
                }
            }
        }

        return true;
    }

    /**
     * @brief 바디의 형상을 현재 자세에서 Delta만큼 쓸어 처음 막히는 바디를 찾습니다.
     */
    bool PhysicsRuntime::FBodyInstance::Sweep(const FVector& Delta, PhysicsRuntime::FBodySweepHit& OutHit, const PhysicsRuntime::FIgnoreBodyPredicate& ShouldIgnore, const FVector& StartOffset) const {
        if (mActor == nullptr || mScene == nullptr || mScene->GetScene() == nullptr || !IsQueryCollisionEnabled() || !Delta.IsFinite() || !StartOffset.IsFinite()) {
            return false;
        }

        const float Length{Delta.Length()};

        if (Length <= 1.0e-6f) {
            return false;
        }

        const Core::FScopedStatTimer StatTimer{PhysicsRuntime::PhysicsStats::SweepQueryTime};
        PhysicsRuntime::PhysicsStats::SweepQueries.Add();

        Core::TArray<physx::PxShape*> Shapes(mActor->getNbShapes());
        mActor->getShapes(Shapes.data(), static_cast<physx::PxU32>(Shapes.size()));

        const physx::PxTransform ActorPose{mActor->getGlobalPose()};
        const physx::PxTransform StartPose{ActorPose.p + PhysicsRuntime::ToPxVec3(StartOffset), ActorPose.q};
        const physx::PxVec3 Direction{PhysicsRuntime::ToPxVec3(Delta / Length)};
        // 시작부터 겹친 바디를 이동 방향으로 걸러야 하므로 Block 바디를 모두 터치로 받아 가장 가까운 것을 직접 고릅니다.
        FResponseQueryFilter Callback{*this, physx::PxQueryHitType::eTOUCH, physx::PxQueryHitType::eNONE, ShouldIgnore};
        const physx::PxQueryFilterData Filter{physx::PxQueryFlag::eSTATIC | physx::PxQueryFlag::eDYNAMIC | physx::PxQueryFlag::ePREFILTER | physx::PxQueryFlag::eNO_BLOCK};
        Core::TArray<physx::PxSweepHit> Touches(256);
        bool Found{};

        for (const physx::PxShape* Shape : Shapes) {
            if (!Shape->getFlags().isSet(physx::PxShapeFlag::eSCENE_QUERY_SHAPE)) {
                continue;
            }

            const physx::PxGeometryHolder Geometry{Shape->getGeometry()};
            physx::PxSweepBuffer Buffer{Touches.data(), static_cast<physx::PxU32>(Touches.size())};

            // 시작부터 겹친 바디의 침투 방향과 깊이를 받기 위해 MTD를 계산합니다.
            if (!mScene->GetScene()->sweep(Geometry.any(), StartPose * Shape->getLocalPose(), Direction, Length, Buffer, physx::PxHitFlag::eDEFAULT | physx::PxHitFlag::eMTD, Filter, &Callback)) {
                continue;
            }

            for (physx::PxU32 Index{}; Index < Buffer.getNbTouches(); ++Index) {
                const physx::PxSweepHit& Touch{Buffer.getTouch(Index)};
                const bool StartPenetrating{Touch.hadInitialOverlap()};
                const FVector Normal{PhysicsRuntime::ToFVector(Touch.normal)};

                // 이미 겹친 바디에서 멀어지는 이동은 막지 않습니다.
                if (StartPenetrating && Delta.Dot(Normal) >= 0.0f) {
                    continue;
                }

                const Core::FLength Distance{StartPenetrating ? Core::FLength{} : Core::FLength::FromBase(Touch.distance)};

                if (Found && Distance >= OutHit.mDistance) {
                    continue;
                }

                Found = true;
                OutHit.mBody = Touch.actor != nullptr ? FromPxActor(*Touch.actor) : nullptr;
                OutHit.mDistance = Distance;
                OutHit.mImpactPoint = PhysicsRuntime::ToFVector(Touch.position);
                OutHit.mImpactNormal = Normal;
                OutHit.mPenetrationDepth = StartPenetrating ? Core::FLength::FromBase(std::max(0.0f, -Touch.distance)) : Core::FLength{};
                OutHit.mStartPenetrating = StartPenetrating;
            }
        }

        return Found;
    }

    /**
     * @brief 바디를 만들 때 동적 강체로 시뮬레이션할지 여부를 반환합니다.
     */
    bool PhysicsRuntime::FBodyInstance::ShouldSimulatePhysics() const {
        return mSimulatePhysics;
    }

    /**
     * @brief 바디를 만들 때 동적 강체로 시뮬레이션할지 설정합니다.
     */
    void PhysicsRuntime::FBodyInstance::SetSimulatePhysics(bool Simulate) {
        mSimulatePhysics = Simulate;
    }

    /**
     * @brief 동적 강체에 중력을 적용할지 반환합니다.
     */
    bool PhysicsRuntime::FBodyInstance::IsGravityEnabled() const {
        return mEnableGravity;
    }

    /**
     * @brief 동적 강체에 중력을 적용할지 설정하고 실제 바디에 반영합니다.
     */
    void PhysicsRuntime::FBodyInstance::SetGravityEnabled(bool Enabled) {
        mEnableGravity = Enabled;

        if (physx::PxRigidDynamic* Dynamic{GetPxRigidDynamic()}) {
            Dynamic->setActorFlag(physx::PxActorFlag::eDISABLE_GRAVITY, !mEnableGravity);

            // 키네마틱 강체는 깨울 수 없으므로 시뮬레이션하는 강체만 깨웁니다.
            if (Dynamic->getScene() != nullptr && !Dynamic->getRigidBodyFlags().isSet(physx::PxRigidBodyFlag::eKINEMATIC)) {
                Dynamic->wakeUp();
            }
        }
    }

    /**
     * @brief 동적 강체에 연속 충돌 검출(CCD)을 사용할지 반환합니다.
     */
    bool PhysicsRuntime::FBodyInstance::IsCCDEnabled() const {
        return mUseCCD;
    }

    /**
     * @brief 동적 강체에 연속 충돌 검출(CCD)을 사용할지 설정하고 실제 바디에 반영합니다.
     */
    void PhysicsRuntime::FBodyInstance::SetCCDEnabled(bool Enabled) {
        mUseCCD = Enabled;

        if (physx::PxRigidDynamic* Dynamic{GetPxRigidDynamic()}) {
            Dynamic->setRigidBodyFlag(physx::PxRigidBodyFlag::eENABLE_CCD, mUseCCD && mSimulatePhysics);
        }
    }

    /**
     * @brief 동적 강체의 지정 질량을 반환합니다.
     */
    Core::FMass PhysicsRuntime::FBodyInstance::GetMassInKg() const {
        return mMassInKg;
    }

    /**
     * @brief 동적 강체의 질량을 설정하고 실제 바디의 질량과 관성을 다시 계산합니다.
     */
    bool PhysicsRuntime::FBodyInstance::SetMassInKg(Core::FMass MassInKg) {
        if (!MassInKg.IsFinite() || MassInKg < Core::FMass{}) {
            return false;
        }

        mMassInKg = MassInKg;

        if (physx::PxRigidDynamic* Dynamic{GetPxRigidDynamic()}) {
            UpdateMassProperties(*Dynamic);
        }

        return true;
    }

    /**
     * @brief 동적 강체의 선형 감쇠 계수를 반환합니다.
     */
    Core::FFrequency PhysicsRuntime::FBodyInstance::GetLinearDamping() const {
        return mLinearDamping;
    }

    /**
     * @brief 동적 강체의 선형 감쇠 계수를 설정하고 실제 바디에 반영합니다.
     */
    bool PhysicsRuntime::FBodyInstance::SetLinearDamping(Core::FFrequency Damping) {
        if (!Damping.IsFinite() || Damping < Core::FFrequency{}) {
            return false;
        }

        mLinearDamping = Damping;

        if (physx::PxRigidDynamic* Dynamic{GetPxRigidDynamic()}) {
            Dynamic->setLinearDamping(mLinearDamping.GetBase());
        }

        return true;
    }

    /**
     * @brief 동적 강체의 회전 감쇠 계수를 반환합니다.
     */
    Core::FFrequency PhysicsRuntime::FBodyInstance::GetAngularDamping() const {
        return mAngularDamping;
    }

    /**
     * @brief 동적 강체의 회전 감쇠 계수를 설정하고 실제 바디에 반영합니다.
     */
    bool PhysicsRuntime::FBodyInstance::SetAngularDamping(Core::FFrequency Damping) {
        if (!Damping.IsFinite() || Damping < Core::FFrequency{}) {
            return false;
        }

        mAngularDamping = Damping;

        if (physx::PxRigidDynamic* Dynamic{GetPxRigidDynamic()}) {
            Dynamic->setAngularDamping(mAngularDamping.GetBase());
        }

        return true;
    }

    /**
     * @brief 동적 강체의 관성 배율을 반환합니다.
     */
    float PhysicsRuntime::FBodyInstance::GetInertiaScale() const {
        return mInertiaScale;
    }

    /**
     * @brief 동적 강체의 관성 배율을 설정하고 실제 바디의 관성을 다시 계산합니다.
     */
    bool PhysicsRuntime::FBodyInstance::SetInertiaScale(float Scale) {
        if (!std::isfinite(Scale) || Scale <= 0.0f) {
            return false;
        }

        mInertiaScale = Scale;

        if (physx::PxRigidDynamic* Dynamic{GetPxRigidDynamic()}) {
            UpdateMassProperties(*Dynamic);
        }

        return true;
    }

    /**
     * @brief 충돌 프리셋을 반환합니다.
     */
    PhysicsRuntime::ECollisionPreset PhysicsRuntime::FBodyInstance::GetCollisionPreset() const {
        return mCollisionPreset;
    }

    /**
     * @brief 충돌 프리셋을 적용하고 실제 바디의 형상에 반영합니다.
     */
    void PhysicsRuntime::FBodyInstance::SetCollisionPreset(PhysicsRuntime::ECollisionPreset Preset) {
        mCollisionPreset = Preset;
        GetPresetSettings(Preset, mCollisionEnabled, mCollisionResponse);
        ApplyCollisionSettings();
    }

    /**
     * @brief 쿼리·시뮬레이션 참여 방식을 반환합니다.
     */
    PhysicsRuntime::ECollisionEnabled PhysicsRuntime::FBodyInstance::GetCollisionEnabled() const {
        return mCollisionEnabled;
    }

    /**
     * @brief 쿼리·시뮬레이션 참여 방식을 바꾸고 실제 바디의 형상에 반영합니다.
     */
    void PhysicsRuntime::FBodyInstance::SetCollisionEnabled(PhysicsRuntime::ECollisionEnabled CollisionEnabled) {
        PhysicsRuntime::ECollisionEnabled PresetEnabled{};
        PhysicsRuntime::ECollisionResponse PresetResponse{};

        if (GetPresetSettings(mCollisionPreset, PresetEnabled, PresetResponse) && PresetEnabled != CollisionEnabled) {
            mCollisionPreset = PhysicsRuntime::ECollisionPreset::Custom;
        }

        mCollisionEnabled = CollisionEnabled;
        ApplyCollisionSettings();
    }

    /**
     * @brief 다른 모든 바디에 대한 응답을 반환합니다.
     */
    PhysicsRuntime::ECollisionResponse PhysicsRuntime::FBodyInstance::GetCollisionResponse() const {
        return mCollisionResponse;
    }

    /**
     * @brief 다른 모든 바디에 대한 응답을 바꾸고 실제 바디의 형상에 반영합니다.
     */
    void PhysicsRuntime::FBodyInstance::SetCollisionResponse(PhysicsRuntime::ECollisionResponse Response) {
        PhysicsRuntime::ECollisionEnabled PresetEnabled{};
        PhysicsRuntime::ECollisionResponse PresetResponse{};

        if (GetPresetSettings(mCollisionPreset, PresetEnabled, PresetResponse) && PresetResponse != Response) {
            mCollisionPreset = PhysicsRuntime::ECollisionPreset::Custom;
        }

        mCollisionResponse = Response;
        ApplyCollisionSettings();
    }

    /**
     * @brief 다른 바디 인스턴스의 충돌 설정만 복사하고 실제 바디의 형상에 반영합니다.
     */
    void PhysicsRuntime::FBodyInstance::CopyCollisionSettings(const FBodyInstance& Source) {
        mCollisionPreset = Source.mCollisionPreset;
        mCollisionEnabled = Source.mCollisionEnabled;
        mCollisionResponse = Source.mCollisionResponse;
        ApplyCollisionSettings();
    }

    /**
     * @brief 장면 쿼리에 참여하는지 반환합니다.
     */
    bool PhysicsRuntime::FBodyInstance::IsQueryCollisionEnabled() const {
        return mCollisionEnabled == PhysicsRuntime::ECollisionEnabled::QueryOnly || mCollisionEnabled == PhysicsRuntime::ECollisionEnabled::QueryAndPhysics;
    }

    /**
     * @brief 충돌 참여 방식과 응답을 실제 바디의 모든 형상 플래그와 필터 데이터에 반영합니다.
     */
    void PhysicsRuntime::FBodyInstance::ApplyCollisionSettings() const {
        if (mActor == nullptr) {
            return;
        }

        const bool Query{IsQueryCollisionEnabled()};
        const bool Simulation{mCollisionEnabled == PhysicsRuntime::ECollisionEnabled::PhysicsOnly || mCollisionEnabled == PhysicsRuntime::ECollisionEnabled::QueryAndPhysics};
        // 시뮬레이션 필터 셰이더와 쿼리 필터가 word0의 응답을 읽어 두 바디 중 약한 응답을 씁니다.
        const physx::PxU32 Response{static_cast<physx::PxU32>(mCollisionResponse)};
        Core::TArray<physx::PxShape*> Shapes(mActor->getNbShapes());
        mActor->getShapes(Shapes.data(), static_cast<physx::PxU32>(Shapes.size()));

        for (physx::PxShape* Shape : Shapes) {
            Shape->setFlag(physx::PxShapeFlag::eSCENE_QUERY_SHAPE, Query);
            Shape->setFlag(physx::PxShapeFlag::eSIMULATION_SHAPE, Simulation);
            physx::PxFilterData QueryFilter{Shape->getQueryFilterData()};
            physx::PxFilterData SimulationFilter{Shape->getSimulationFilterData()};
            QueryFilter.word0 = Response;
            SimulationFilter.word0 = Response;
            Shape->setQueryFilterData(QueryFilter);
            Shape->setSimulationFilterData(SimulationFilter);
        }

        if (physx::PxScene* Scene{mActor->getScene()}) {
            Scene->resetFiltering(*mActor);
        }
    }

    /**
     * @brief 이미 만든 움직이는 강체의 시뮬레이션 여부를 다시 만들지 않고 바꿉니다.
     */
    void PhysicsRuntime::FBodyInstance::SetInstanceSimulatePhysics(bool Simulate) {
        mSimulatePhysics = Simulate;

        physx::PxRigidDynamic* Dynamic{GetPxRigidDynamic()};

        if (Dynamic == nullptr) {
            return;
        }

        if (!Simulate) {
            UnregisterVehicle(mScene, *this);
        }

        // CCD가 켜진 채로 키네마틱이 될 수 없으므로 CCD를 먼저 끄고 마지막에 다시 맞춥니다.
        Dynamic->setRigidBodyFlag(physx::PxRigidBodyFlag::eENABLE_CCD, false);
        Dynamic->setRigidBodyFlag(physx::PxRigidBodyFlag::eKINEMATIC, !Simulate);
        Dynamic->setRigidBodyFlag(physx::PxRigidBodyFlag::eENABLE_CCD, mUseCCD && Simulate);

        // 키네마틱에서 풀려난 강체가 잠든 채로 멈춰 있지 않게 깨웁니다.
        if (Simulate && Dynamic->getScene() != nullptr) {
            Dynamic->wakeUp();
        }
    }

    /**
     * @brief 질량·감쇠·중력·CCD 설정을 동적 강체에 반영합니다.
     */
    void PhysicsRuntime::FBodyInstance::ApplyDynamicSettings(physx::PxRigidDynamic& Dynamic) const {
        Dynamic.setLinearDamping(mLinearDamping.GetBase());
        Dynamic.setAngularDamping(mAngularDamping.GetBase());
        Dynamic.setActorFlag(physx::PxActorFlag::eDISABLE_GRAVITY, !mEnableGravity);
        // 키네마틱 강체에는 CCD를 켤 수 없으므로 시뮬레이션할 때만 켭니다.
        Dynamic.setRigidBodyFlag(physx::PxRigidBodyFlag::eENABLE_CCD, mUseCCD && mSimulatePhysics);
        UpdateMassProperties(Dynamic);
    }

    /**
     * @brief 질량 설정으로 동적 강체의 질량과 관성을 다시 계산합니다.
     */
    void PhysicsRuntime::FBodyInstance::UpdateMassProperties(physx::PxRigidDynamic& Dynamic) const {
        bool Updated{};

        if (mMassInKg > Core::FMass{}) {
            Updated = physx::PxRigidBodyExt::setMassAndUpdateInertia(Dynamic, mMassInKg.GetBase());
        } else if (mScene != nullptr && mScene->GetRuntime() != nullptr) {
            Updated = physx::PxRigidBodyExt::updateMassAndInertia(Dynamic, mScene->GetRuntime()->GetSettings().mDefaultDensity.GetBase());
        }

        if (!Updated) {
            MACAW_LOG(LogPhysics, Warning, "Cannot update the mass properties of a dynamic body.");

            return;
        }

        // 위에서 형상으로 관성을 새로 계산했으므로 배율을 한 번만 곱해도 호출을 반복해도 값이 누적되지 않는다.
        if (mInertiaScale != 1.0f) {
            Dynamic.setMassSpaceInertiaTensor(Dynamic.getMassSpaceInertiaTensor() * mInertiaScale);
        }
    }

}
