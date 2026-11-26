/**
 * @file FRigidBodySimulation.cpp
 * @brief 피직스 에셋의 바디와 관절로 월드와 분리된 독립 물리 장면을 만들어 고정 스텝으로 시뮬레이션합니다.
 * @ingroup PhysicsRuntime
 */

#include "PhysicsRuntime/FRigidBodySimulation.h"

#include "Core/Math/FQuat.h"
#include "PhysicsRuntime/FPhysicsRuntime.h"
#include "PhysicsRuntime/UBodySetup.h"
#include "PhysicsRuntime/UPhysicsAsset.h"

#include <algorithm>

namespace PhysicsRuntime {

    FRigidBodySimulation::FRigidBodySimulation() = default;

    FRigidBodySimulation::~FRigidBodySimulation() {
        Shutdown();
    }

    bool FRigidBodySimulation::Initialize(PhysicsRuntime::FPhysicsRuntime& Runtime, const PhysicsRuntime::UPhysicsAsset& Asset, const FBoneWorldFunction& GetBoneWorld, const FVector& Scale3D) {
        Shutdown();

        if (!GetBoneWorld || !Runtime.IsInitialized()) {
            return false;
        }

        mScene = std::make_unique<PhysicsRuntime::FPhysicsScene>();

        if (!mScene->Initialize(Runtime)) {
            mScene.reset();

            return false;
        }

        // 관절 위치에는 축별 스케일 중 가장 작은 값을 곱한다. 컴포넌트의 물리 에셋과 같은 규칙이다.
        const float ActualScale{std::min({std::abs(Scale3D.x), std::abs(Scale3D.y), std::abs(Scale3D.z)})};
        const float Scale{ActualScale == 0.0f ? 0.0001f : ActualScale};
        const Index_t BodyCount{Asset.mSkeletalBodySetups.size()};
        Core::TMap<Core::FString, PhysicsRuntime::FBodyInstance*> NameToBody{};
        Index_t Created{};
        const Uint32 CollisionGroup{mScene->AllocateCollisionGroup()};

        mEntries.resize(BodyCount);

        for (Index_t BodyIndex{}; BodyIndex < BodyCount; ++BodyIndex) {
            const PhysicsRuntime::UBodySetup* Setup{Asset.mSkeletalBodySetups[BodyIndex].get()};

            if (Setup == nullptr) {
                continue;
            }

            FTransform BoneWorld{};

            if (!GetBoneWorld(Setup->GetBoneName(), BoneWorld)) {
                continue;
            }

            // 바디를 만들기 전에 Dynamic 여부를 정해야 PhysX 강체의 Kinematic 플래그가 맞게 만들어진다.
            auto Body{std::make_unique<PhysicsRuntime::FBodyInstance>()};

            Body->SetSimulatePhysics(Setup->ShouldSimulatePhysics());
            Body->SetLinearDamping(Setup->GetLinearDamping());
            Body->SetAngularDamping(Setup->GetAngularDamping());
            Body->SetMassInKg(Setup->GetMassInKg());
            Body->SetInertiaScale(Setup->GetInertiaScale());
            Body->mInstanceBodyIndex = BodyIndex;

            PhysicsRuntime::FInitBodySpawnParams SpawnParams{};
            SpawnParams.mStaticPhysics = false;

            // 이 장면의 바디는 컴포넌트에 속하지 않으므로 소유자는 두지 않는다.
            if (!Body->InitBody(*Setup, BoneWorld, nullptr, *mScene, SpawnParams)) {
                continue;
            }

            // 충돌 설정: 이 에셋의 바디끼리는 비활성 쌍 목록을 따르고, 충돌을 끈 바디는 아무것도 부딪히지 않는다.
            Body->SetCollisionFilter(CollisionGroup, static_cast<Uint32>(BodyIndex), Setup->IsCollisionEnabled());

            // 머리카락처럼 가벼운 바디가 접촉이 없어 잠들면 Kinematic 부모가 움직여도 늦게 반응하므로 잠들지 않게 한다.
            if (physx::PxRigidDynamic* Dynamic{Body->GetPxRigidActor() != nullptr ? Body->GetPxRigidActor()->is<physx::PxRigidDynamic>() : nullptr}) {
                Dynamic->setSleepThreshold(0.0f);
            }

            FEntry& Entry{mEntries[BodyIndex]};

            Entry.mBoneName = Setup->GetBoneName();
            Entry.mSimulated = Setup->ShouldSimulatePhysics();
            Entry.mWindInfluence = Entry.mSimulated ? Setup->GetWindInfluence() : 0.0f;
            NameToBody[Entry.mBoneName] = Body.get();
            Entry.mBody = std::move(Body);
            ++Created;
        }

        if (Created == 0) {
            Shutdown();

            return false;
        }

        mScene->SetCollisionDisabledPairs(CollisionGroup, Asset.BuildCollisionDisabledBodyPairs());

        mConstraints.resize(Asset.mConstraintSetup.size());

        for (Index_t ConstraintIndex{}; ConstraintIndex < Asset.mConstraintSetup.size(); ++ConstraintIndex) {
            const PhysicsRuntime::UPhysicsConstraintTemplate* Template{Asset.mConstraintSetup[ConstraintIndex].get()};

            if (Template == nullptr) {
                continue;
            }

            auto Constraint{std::make_unique<PhysicsRuntime::FConstraintInstance>()};
            Constraint->CopyConstraintParamsFrom(Template->mDefaultInstance);

            // 관절이 잇는 두 뼈에 모두 이 장면의 바디가 있을 때만 잇는다.
            const auto Body1{NameToBody.find(Constraint->mConstraintBone1)};
            const auto Body2{NameToBody.find(Constraint->mConstraintBone2)};

            if (Body1 != NameToBody.end() && Body2 != NameToBody.end() && Constraint->InitConstraint(Body1->second, Body2->second, Scale)) {
                mConstraints[ConstraintIndex] = std::move(Constraint);
            }
        }

        return true;
    }

    void FRigidBodySimulation::Shutdown() {
        // 관절 -> 바디 -> 장면 순서로 해제한다. 바디는 장면보다 먼저 제거되어야 한다.
        for (const auto& Constraint : mConstraints) {
            if (Constraint != nullptr) {
                Constraint->TermConstraint();
            }
        }

        mConstraints.clear();

        for (FEntry& Entry : mEntries) {
            if (Entry.mBody != nullptr) {
                Entry.mBody->TermBody();
            }
        }

        mEntries.clear();
        mScene.reset();
        mAccumulator = {};
    }

    bool FRigidBodySimulation::IsInitialized() const {
        return mScene != nullptr && mScene->IsInitialized();
    }

    Index_t FRigidBodySimulation::GetBodyCount() const {
        return mEntries.size();
    }

    bool FRigidBodySimulation::HasBody(Index_t BodyIndex) const {
        return BodyIndex < mEntries.size() && mEntries[BodyIndex].mBody != nullptr && mEntries[BodyIndex].mBody->IsValidBodyInstance();
    }

    const Core::FString& FRigidBodySimulation::GetBoneName(Index_t BodyIndex) const {
        static const Core::FString Empty{};

        return BodyIndex < mEntries.size() ? mEntries[BodyIndex].mBoneName : Empty;
    }

    bool FRigidBodySimulation::IsSimulated(Index_t BodyIndex) const {
        return HasBody(BodyIndex) && mEntries[BodyIndex].mSimulated;
    }

    void FRigidBodySimulation::SetKinematicTarget(Index_t BodyIndex, const FTransform& World) {
        if (!HasBody(BodyIndex) || mEntries[BodyIndex].mSimulated) {
            return;
        }

        FEntry& Entry{mEntries[BodyIndex]};

        // 처음 지정한 목표는 이전 목표로도 삼아, 시작하자마자 큰 속도가 생기지 않게 한다.
        if (!Entry.mHasTarget) {
            Entry.mPreviousTarget = World;
            Entry.mHasTarget = true;
        }

        Entry.mTarget = World;
    }

    float FRigidBodySimulation::GetWindInfluence(Index_t BodyIndex) const {
        return HasBody(BodyIndex) ? mEntries[BodyIndex].mWindInfluence : 0.0f;
    }

    void FRigidBodySimulation::SetBodyWind(Index_t BodyIndex, const FVector& WindVelocity) {
        if (!HasBody(BodyIndex) || mEntries[BodyIndex].mWindInfluence <= 0.0f) {
            return;
        }

        mEntries[BodyIndex].mWind = WindVelocity.IsFinite() ? WindVelocity : FVector{};
    }

    void FRigidBodySimulation::Advance(Core::FTime DeltaTime) {
        if (!IsInitialized() || !DeltaTime.IsFinite()) {
            return;
        }

        const PhysicsRuntime::FPhysicsSettings& Settings{mScene->GetRuntime()->GetSettings()};
        const Core::FTime FixedStep{Settings.mFixedStep};

        if (FixedStep <= Core::FTime{}) {
            return;
        }

        // 긴 프레임을 따라잡느라 스텝이 폭주하지 않게 누적 시간의 상한을 둔다.
        mAccumulator = std::min(mAccumulator + std::max(DeltaTime, Core::FTime{}), FixedStep * static_cast<float>(Settings.mMaxStepsPerFrame));

        const Uint32 Steps{static_cast<Uint32>(mAccumulator / FixedStep)};

        if (Steps == 0) {
            return;
        }

        mAccumulator -= FixedStep * static_cast<float>(Steps);

        for (Uint32 Step{1}; Step <= Steps; ++Step) {
            // 스텝마다 이전 목표에서 현재 목표까지 나눠 주어, 한 프레임 안에서 Kinematic 바디가 일정한 속도로 움직이게 한다.
            const float Alpha{static_cast<float>(Step) / static_cast<float>(Steps)};

            for (FEntry& Entry : mEntries) {
                if (Entry.mBody == nullptr || Entry.mSimulated || !Entry.mHasTarget) {
                    continue;
                }

                FTransform Interpolated{};

                Interpolated.mLocation = Entry.mPreviousTarget.mLocation.Lerp(Entry.mTarget.mLocation, Alpha);
                Interpolated.mRotation = Entry.mPreviousTarget.mRotation.Slerp(Entry.mTarget.mRotation, Alpha).Normalized();
                Interpolated.mScale = Entry.mTarget.mScale;
                Entry.mBody->SetBodyTransform(Interpolated);
            }

            // 바람: 스텝마다 상대 속도에 비례하는 가속도를 준다. 힘은 한 스텝이 끝나면 지워지므로 매 스텝 다시 준다.
            for (FEntry& Entry : mEntries) {
                if (Entry.mBody == nullptr || Entry.mWindInfluence <= 0.0f || Entry.mWind.IsNearlyZero()) {
                    continue;
                }

                Entry.mBody->AddForce((Entry.mWind - Entry.mBody->GetLinearVelocity()) * Entry.mWindInfluence, true);
            }

            mScene->Simulate(FixedStep);
        }

        for (FEntry& Entry : mEntries) {
            Entry.mPreviousTarget = Entry.mTarget;
        }
    }

    bool FRigidBodySimulation::GetBodyTransform(Index_t BodyIndex, FTransform& OutWorld) const {
        if (!HasBody(BodyIndex)) {
            return false;
        }

        OutWorld = mEntries[BodyIndex].mBody->GetBodyTransform();
        OutWorld.mScale = mEntries[BodyIndex].mBody->GetBodyScale();

        return true;
    }

}
