#include "Engine/Particles/Module/UParticleModuleCollision.h"
#include "Core/Literals/UnitLiterals.h"
#include "Engine/Particles/FBaseParticle.h"
#include "Engine/Particles/FParticleEmitterInstance.h"
#include "Engine/Particles/ParticleHelper.h"
#include "Engine/Component/UParticleSystemComponent.h"
#include "Engine/Component/UPrimitiveComponent.h"
#include "Engine/AActor.h"
#include "Engine/UWorld.h"
#include "PhysicsRuntime/FBodyInstance.h"
#include "PhysicsRuntime/FPhysicsScene.h"

#include <algorithm>
#include <cmath>

using namespace Core::Literals;

namespace Engine {

    Engine::UParticleModuleCollision::UParticleModuleCollision() {
        SetName("Collision");
        mSpawnModule = true;
        mUpdateModule = true;
        mRestitution.mConstant = 0.5f;
        mFriction.mConstant = 0.2f;
        mRotationDamping.mConstant = 1.0f;

        // Build 전에도 기본값으로 동작하도록 미리 굽습니다.
        BakeDistributions();
    }

    Engine::UParticleModuleCollision::~UParticleModuleCollision() = default;

    void Engine::UParticleModuleCollision::Spawn(Engine::FParticleEmitterInstance& Owner, Int32 Offset, float, Engine::FBaseParticle& Particle) const {
        // 파티클마다 값을 뽑아 저장해 같은 이펙트 안에서도 튀는 정도가 다릅니다.
        Engine::FParticleCollisionPayload& Payload{Engine::ParticleHelper::GetPayload<Engine::FParticleCollisionPayload>(Particle, Offset)};

        Payload.mRestitution = mRestitution.GetValue(Owner.mEmitterTime, Owner.mRandomStream);
        Payload.mFriction = mFriction.GetValue(Owner.mEmitterTime, Owner.mRandomStream);
        Payload.mRotationDamping = mRotationDamping.GetValue(Owner.mEmitterTime, Owner.mRandomStream);
        Payload.mDelay = mDelayAmount.GetValue(Owner.mEmitterTime, Owner.mRandomStream);
        Payload.mRemainingCollisions = mMaxCollisions;
        Payload.mInitialized = 1;
    }

    void Engine::UParticleModuleCollision::Update(Engine::FParticleEmitterInstance& Owner, Int32 Offset, float DeltaTime) const {
        Engine::UWorld* World{Owner.mComponent != nullptr ? Owner.mComponent->GetWorld() : nullptr};
        const PhysicsRuntime::FPhysicsScene* Scene{World != nullptr ? World->GetPhysicsScene() : nullptr};

        if (DeltaTime <= 0.0f || Scene == nullptr || !Scene->IsInitialized()) {
            return;
        }

        const Engine::AActor* IgnoreActor{mIgnoreSourceActor ? Owner.mComponent->GetOwner() : nullptr};
        const PhysicsRuntime::FIgnoreBodyPredicate ShouldIgnore{[IgnoreActor](const PhysicsRuntime::FBodyInstance& Body) {
            const Engine::UPrimitiveComponent* Component{Body.GetOwnerComponent()};

            return IgnoreActor != nullptr && Component != nullptr && Component->GetOwner() == IgnoreActor;
        }};

        Engine::ParticleHelper::ForEachParticle(Owner, [&](Engine::FBaseParticle& Particle, Uint8* Base) {
            if ((Particle.mFlags & Engine::EParticleState::IgnoreCollisions) != 0) {
                return;
            }

            Engine::FParticleCollisionPayload& Payload{Engine::ParticleHelper::GetPayload<Engine::FParticleCollisionPayload>(Base, Offset)};

            if (Payload.mInitialized == 0 || (Payload.mDelay > 0.0f && Particle.mOneOverMaxLifetime > 0.0f && Particle.mRelativeTime / Particle.mOneOverMaxLifetime < Payload.mDelay)) {
                return;
            }

            // 이번 프레임에 실제로 움직일 경로입니다. 적분은 모든 모듈 뒤에 일어납니다.
            const FVector StartSimulation{Particle.mLocation};
            const FVector DeltaSimulation{Particle.mVelocity * DeltaTime};
            const FVector StartWorld{Owner.SimulationToWorld(StartSimulation)};
            const FVector Segment{Owner.SimulationToWorld(StartSimulation + DeltaSimulation) - StartWorld};
            const Core::FLength Length{Core::FLength::FromBase(Segment.Length())};

            if (Length <= 0.1_mm) {
                return;
            }

            const Core::FLength Radius{Core::FLength::FromBase(std::max(Particle.mSize.x, Particle.mSize.y)) * mRadiusScale};
            PhysicsRuntime::FBodySweepHit Hit{};

            if (!Scene->SweepSphere(StartWorld, Segment, Radius, Hit, ShouldIgnore)) {
                return;
            }

            // 경로상 비율은 아핀 변환에 상관없이 같으므로 응답은 시뮬레이션 공간에서 계산합니다.
            const float T{std::clamp(static_cast<float>(Hit.mDistance / Length), 0.0f, 1.0f)};
            const FVector Normal{Owner.WorldToSimulationVector(Hit.mImpactNormal).Normalized()};
            const FVector HitPoint{StartSimulation + DeltaSimulation * T + Normal * mSurfaceOffset.GetBase()};
            // 첫 충돌은 아직 충돌 표시가 없는 경우, 마지막 충돌은 이번에 남은 횟수가 0이 되는 경우입니다.
            const bool FirstHit{(Particle.mFlags & Engine::EParticleState::CollisionHasOccurred) == 0};
            const bool LastHit{mMaxCollisions > 0 && Payload.mRemainingCollisions == 1};
            Engine::UPrimitiveComponent* HitComponent{Hit.mBody != nullptr ? Hit.mBody->GetOwnerComponent() : nullptr};

            // 반사 전에 보고해야 이벤트의 속도와 방향이 들어오던 방향입니다.
            Owner.ReportCollisionEvent(Particle, Owner.SimulationToWorld(HitPoint), Hit.mImpactNormal, T, HitComponent, FirstHit, LastHit);

            // V' = Vt × (1 − 마찰) − Vn × 반발 계수
            const FVector NormalVelocity{Normal * Particle.mVelocity.Dot(Normal)};
            const FVector TangentVelocity{Particle.mVelocity - NormalVelocity};
            FVector NewVelocity{TangentVelocity * (1.0f - Payload.mFriction) - NormalVelocity * Payload.mRestitution};
            const Core::FSpeed NormalSpeed{Core::FSpeed::FromBase(NewVelocity.Dot(Normal))};

            // 튕기는 힘이 작으면 법선 성분을 없애 바닥에서 떨지 않게 합니다.
            if (Core::Math::Abs(NormalSpeed) < mMinBounceSpeed) {
                NewVelocity -= Normal * NormalSpeed.GetBase();
            }

            // 충돌은 운동 상태 자체를 바꾸므로 기준 속도에도 기록해 매 프레임 되돌림에서 사라지지 않게 합니다.
            Particle.mVelocity = NewVelocity;
            Particle.mBaseVelocity = NewVelocity;
            Particle.mRotationRate *= Payload.mRotationDamping;
            Particle.mBaseRotationRate *= Payload.mRotationDamping;
            // 적분 뒤 HitPoint + V' × (1 − t)·dt에 닿도록 미리 당겨 둡니다.
            Particle.mLocation = HitPoint - NewVelocity * (T * DeltaTime);
            Particle.mFlags |= Engine::EParticleState::CollisionHasOccurred;

            if (mMaxCollisions <= 0 || --Payload.mRemainingCollisions > 0) {
                return;
            }

            const auto StopAtHit{[&Particle, &HitPoint]() {
                Particle.mLocation = HitPoint;
                Particle.mVelocity = {};
                Particle.mBaseVelocity = {};
            }};

            switch (mCompletionOption) {
            case Engine::EParticleCollisionComplete::Kill:
                // 다음 Tick의 제거 단계가 지웁니다. 이번 프레임은 충돌 지점에 둡니다.
                StopAtHit();
                Particle.mRelativeTime = 1.1f;
                break;
            case Engine::EParticleCollisionComplete::Freeze:
                StopAtHit();
                Particle.mRotationRate = 0.0f;
                Particle.mBaseRotationRate = 0.0f;
                Particle.mFlags |= Engine::EParticleState::Freeze;
                break;
            case Engine::EParticleCollisionComplete::HaltCollisions:
                Particle.mFlags |= Engine::EParticleState::IgnoreCollisions;
                break;
            case Engine::EParticleCollisionComplete::FreezeTranslation:
                StopAtHit();
                Particle.mFlags |= Engine::EParticleState::FreezeTranslation;
                break;
            case Engine::EParticleCollisionComplete::FreezeRotation:
                Particle.mFlags |= Engine::EParticleState::FreezeRotation;
                break;
            case Engine::EParticleCollisionComplete::FreezeMovement:
                StopAtHit();
                Particle.mFlags |= Engine::EParticleState::FreezeTranslation | Engine::EParticleState::FreezeRotation;
                break;
            }
        });
    }

    void Engine::UParticleModuleCollision::BakeDistributions() {
        mRestitution.Bake();
        mFriction.Bake();
        mRotationDamping.Bake();
        mDelayAmount.Bake();
    }

    Uint32 Engine::UParticleModuleCollision::RequiredBytes() const {
        return sizeof(Engine::FParticleCollisionPayload);
    }

}
