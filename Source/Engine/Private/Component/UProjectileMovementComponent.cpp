/**
 * @file UProjectileMovementComponent.cpp
 * @brief Game과 PIE 월드에서 초기 속도·중력·최대 속도에 따라 소유 액터를 이동하는 발사체 컴포넌트입니다.
 * @ingroup Engine
 */

#include "Engine/Component/UProjectileMovementComponent.h"
#include "Engine/Collision/FHitResult.h"
#include "Engine/Component/USceneComponent.h"
#include "Engine/UWorld.h"
#include "Core/Math/FRotator.h"
#include "Core/Literals.h"

#include <algorithm>
#include <cmath>

using namespace Core::Literals;

namespace Engine {

    /**
     * @brief 기본 설정으로 UProjectileMovementComponent 객체를 초기화합니다.
     */
    Engine::UProjectileMovementComponent::UProjectileMovementComponent()
		: mInitialVelocity{(10_mps).GetBase(), 0.0f, (5_mps).GetBase()},
		  mMaxSpeed{100_mps} {
    }

    /**
     * @brief 객체 수명을 끝내고 소유한 자원을 정리합니다.
     */
    Engine::UProjectileMovementComponent::~UProjectileMovementComponent() = default;

    /**
     * @brief 초기 속도를 필요 시 월드 공간으로 변환하고 최대 속도를 적용하여 정지 상태를 해제합니다.
     */
    void Engine::UProjectileMovementComponent::BeginPlay() {
        FVector Velocity{mInitialVelocity};
        const Engine::USceneComponent* Component{GetUpdatedComponent()};

        if (mInitialVelocityInLocalSpace && Component != nullptr) {
            Velocity = Component->GetTransform().mRotation.RotateVector(Velocity);
        }

        if (!SetVelocity(Velocity)) {
            StopSimulating();
        }
    }

    /**
     * @brief Game과 PIE에서 중력·최대 속도를 반영하여 하위 시간 간격으로 적분하고 필요 시 속도 방향으로 회전합니다.
     */
    void Engine::UProjectileMovementComponent::Tick(float DeltaTime) {
        Engine::UWorld* World{GetWorld()};
        const Core::FTime FrameTime{Core::FTime::FromBase(DeltaTime)};

        if (World == nullptr || (World->GetWorldType() != Engine::EWorldType::Game && World->GetWorldType() != Engine::EWorldType::PIE) || !HasBegunPlay() || mStopped || ShouldSkipUpdate(FrameTime)) {
            return;
        }

        constexpr Core::FTime MaxStepTime{1.0f / 120_Hz};
        const float StepCount{FrameTime / MaxStepTime};
        const auto StepTime{FrameTime / std::clamp(std::ceil(StepCount), 1.0f, 128.0f)};
        const FTransform Start{GetUpdatedComponent()->GetTransform()};
        const bool Sweep{mSweepCollision};
        Engine::UWorld::FDispatchScope Scope{Sweep ? World : nullptr};
        FVector Location{Start.mLocation};
        FVector Velocity{GetVelocity()};
        auto Remaining{FrameTime};

        while (Remaining > 0_s) {
            const auto Step{std::min(StepTime, Remaining)};

            if (Sweep) {
                if (!SimulateSweptStep(Step)) {
                    return;
                }
            } else {
                // 스윕을 끄면 기존과 같이 월드 위치를 서브스텝마다 적분해 프레임 끝에 한 번 적용합니다.
                const FVector NextVelocity{ComputeVelocity(Velocity, Step)};
                Location += ComputeMoveDelta(Velocity, NextVelocity, Step);
                Velocity = NextVelocity;
            }

            Remaining -= Step;
        }

        if (!Sweep && Engine::UMovementComponent::SetVelocity(Velocity)) {
            MoveUpdatedComponent(Location - Start.mLocation, ComputeMoveRotation(Start.mRotation, Velocity));
        }
    }

    FVector Engine::UProjectileMovementComponent::LimitVelocity(const FVector& Velocity) const {
        return mMaxSpeed > 0_mps ? Velocity.ClampedLength(mMaxSpeed.GetBase()) : Velocity;
    }

    FVector Engine::UProjectileMovementComponent::ComputeVelocity(const FVector& Velocity, Core::FTime StepTime) const {
        constexpr Core::FAcceleration StandardGravity{9.81_mps2};
        const auto VerticalSpeedDelta{-StandardGravity * mGravityScale * StepTime};

        return LimitVelocity(Velocity + FVector{0.0f, 0.0f, VerticalSpeedDelta.GetBase()});
    }

    FVector Engine::UProjectileMovementComponent::ComputeMoveDelta(const FVector& Velocity, const FVector& NextVelocity, Core::FTime StepTime) {
        return (Velocity + NextVelocity) * (0.5f * StepTime.GetBase());
    }

    FQuat Engine::UProjectileMovementComponent::ComputeMoveRotation(const FQuat& Rotation, const FVector& Velocity) const {
        using FSpeedSquared = decltype(1_mps * 1_mps);
        constexpr auto MinSpeedSquared{FSpeedSquared::FromBase(0.000001f)};
        const auto SpeedSquared{FSpeedSquared::FromBase(Velocity.LengthSquared())};
        return mRotationFollowsVelocity && SpeedSquared > MinSpeedSquared ? Velocity.Rotation().Quaternion() : Rotation;
    }

    bool Engine::UProjectileMovementComponent::SimulateSweptStep(Core::FTime StepTime) {
        // 0초에 반복해서 닿는 경우에도 한 서브스텝에서 처리할 충돌 횟수를 제한합니다.
        constexpr int MaxImpactsPerStep{4};
        auto Remaining{StepTime};

        for (int ImpactCount{}; ImpactCount < MaxImpactsPerStep && Remaining > 0_s; ++ImpactCount) {
            const Engine::USceneComponent* Component{GetUpdatedComponent()};

            if (Component == nullptr) {
                return false;
            }

            const auto Target{CoreUObject::MakeObjectRef(Component)};
            const FVector Velocity{GetVelocity()};
            const FVector NextVelocity{ComputeVelocity(Velocity, Remaining)};
            const FVector Delta{ComputeMoveDelta(Velocity, NextVelocity, Remaining)};
            const FQuat Rotation{ComputeMoveRotation(Component->GetTransform().mRotation, NextVelocity)};

            if (!Engine::UMovementComponent::SetVelocity(NextVelocity)) {
                StopSimulating();
                return false;
            }

            Engine::FHitResult Hit{};
            const bool Moved{SafeMoveUpdatedComponent(Delta, Rotation, true, Hit)};

            // 충돌 콜백에서 정지하거나 대상·속도를 바꾼 경우에는 콜백이 지정한 상태를 유지합니다.
            if (mStopped || !Target.IsValid() || GetUpdatedComponent() != Target.Get() || GetVelocity() != NextVelocity) {
                return false;
            }

            if (!Hit.mBlockingHit) {
                return Moved;
            }

            const auto ImpactTime{Remaining * Hit.mTime};

            if (!HandleImpact(Hit, ComputeVelocity(Velocity, ImpactTime))) {
                return false;
            }

            if (mRotationFollowsVelocity) {
                MoveUpdatedComponent({}, ComputeMoveRotation(Rotation, GetVelocity()));
            }

            Remaining = ImpactTime >= Remaining ? 0_s : Remaining - ImpactTime;
        }

        return true;
    }

    bool Engine::UProjectileMovementComponent::HandleImpact(const Engine::FHitResult& Hit, const FVector& ImpactVelocity) {
        if (mShouldBounce && !Hit.mStartPenetrating && Hit.mNormal.IsNormalized()) {
            const auto NormalSpeed{Core::FSpeed::FromBase(ImpactVelocity.Dot(Hit.mNormal))};

            if (NormalSpeed < 0_mps) {
                // 표면에 평행한 속도는 그대로 두고 수직 성분에만 반발 계수를 적용합니다.
                const auto NormalSpeedDelta{(1.0f + mBounciness) * NormalSpeed};
                const FVector ReflectedVelocity{LimitVelocity(ImpactVelocity - Hit.mNormal * NormalSpeedDelta.GetBase())};

                if (!ReflectedVelocity.IsNearlyZero() && Engine::UMovementComponent::SetVelocity(ReflectedVelocity)) {
                    return true;
                }
            }
        }

        StopSimulating();

        return false;
    }

    /**
     * @brief 실행 종료 시 속도를 0으로 만들고 발사체 이동을 정지합니다.
     */
    void Engine::UProjectileMovementComponent::EndPlay() {
        StopSimulating();
    }

    /**
     * @brief 현재 속도를 0으로 만들고 다음 속도 설정 또는 실행 시작까지 이동을 정지합니다.
     */
    void Engine::UProjectileMovementComponent::StopMovement() {
        StopSimulating();
    }

    void Engine::UProjectileMovementComponent::StopMovementImmediately() {
        StopSimulating();
    }

    void Engine::UProjectileMovementComponent::StopSimulating() {
        Engine::UMovementComponent::StopMovementImmediately();
        mStopped = true;
    }

    /**
     * @brief 실행 시작에 사용할 초기 속도 설정을 반환합니다.
     */
    const FVector& Engine::UProjectileMovementComponent::GetInitialVelocity() const {
        return mInitialVelocity;
    }

    /**
     * @brief 유한한 초기 속도를 저장하며 현재 실행 중인 속도는 즉시 바꾸지 않습니다.
     */
    bool Engine::UProjectileMovementComponent::SetInitialVelocity(const FVector& Velocity) {
        if (!Velocity.IsFinite()) {
            return false;
        }

        mInitialVelocity = Velocity;

        return true;
    }

    /**
     * @brief 현재 월드 속도를 설정하고 최대 속도로 제한한 뒤 정지 상태를 해제합니다.
     */
    bool Engine::UProjectileMovementComponent::SetVelocity(const FVector& Velocity) {
        if (!Velocity.IsFinite() || !Engine::UMovementComponent::SetVelocity(LimitVelocity(Velocity))) {
            return false;
        }

        mStopped = false;

        return true;
    }

    /**
     * @brief 속도 크기의 상한을 반환합니다.
     */
    Core::FSpeed Engine::UProjectileMovementComponent::GetMaxSpeed() const {
        return mMaxSpeed;
    }

    /**
     * @brief 0 이상의 유한한 최대 속도를 저장하고 필요한 경우 현재 속도를 제한합니다.
     */
    bool Engine::UProjectileMovementComponent::SetMaxSpeed(Core::FSpeed Speed) {
        if (!Speed.IsFinite() || Speed < 0_mps) {
            return false;
        }

        mMaxSpeed = Speed;

        if (mMaxSpeed > 0_mps) {
            Engine::UMovementComponent::SetVelocity(LimitVelocity(GetVelocity()));
        }

        return true;
    }

    /**
     * @brief -Z 방향 기본 중력 9.81에 곱할 배율을 반환합니다.
     */
    float Engine::UProjectileMovementComponent::GetGravityScale() const {
        return mGravityScale;
    }

    /**
     * @brief 0~100 범위의 유한한 중력 배율을 설정합니다.
     */
    bool Engine::UProjectileMovementComponent::SetGravityScale(float Scale) {
        if (!std::isfinite(Scale) || Scale < 0.0f || Scale > 100.0f) {
            return false;
        }

        mGravityScale = Scale;

        return true;
    }

    /**
     * @brief 초기 속도를 이동 대상의 로컬 회전으로 변환하는지 조회합니다.
     */
    bool Engine::UProjectileMovementComponent::GetInitialVelocityInLocalSpace() const {
        return mInitialVelocityInLocalSpace;
    }

    /**
     * @brief 실행 시작 시 초기 속도를 로컬 또는 월드 공간으로 해석하도록 설정합니다.
     */
    bool Engine::UProjectileMovementComponent::SetInitialVelocityInLocalSpace(bool LocalSpace) {
        mInitialVelocityInLocalSpace = LocalSpace;

        return true;
    }

    /**
     * @brief 이동 대상의 전방을 현재 속도 방향에 맞추는지 조회합니다.
     */
    bool Engine::UProjectileMovementComponent::GetRotationFollowsVelocity() const {
        return mRotationFollowsVelocity;
    }

    /**
     * @brief 이동 후 속도가 충분히 클 때 이동 대상의 회전이 속도 방향을 따르도록 설정합니다.
     */
    bool Engine::UProjectileMovementComponent::SetRotationFollowsVelocity(bool FollowsVelocity) {
        mRotationFollowsVelocity = FollowsVelocity;

        return true;
    }

    bool Engine::UProjectileMovementComponent::GetSweepCollision() const {
        return mSweepCollision;
    }

    void Engine::UProjectileMovementComponent::SetSweepCollision(bool SweepCollision) {
        mSweepCollision = SweepCollision;
    }

    bool Engine::UProjectileMovementComponent::GetShouldBounce() const {
        return mShouldBounce;
    }

    void Engine::UProjectileMovementComponent::SetShouldBounce(bool ShouldBounce) {
        mShouldBounce = ShouldBounce;
    }

    float Engine::UProjectileMovementComponent::GetBounciness() const {
        return mBounciness;
    }

    bool Engine::UProjectileMovementComponent::SetBounciness(float Bounciness) {
        if (!std::isfinite(Bounciness) || Bounciness < 0.0f || Bounciness > 1.0f) {
            return false;
        }

        mBounciness = Bounciness;

        return true;
    }

}
