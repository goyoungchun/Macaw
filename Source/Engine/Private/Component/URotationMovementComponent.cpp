/**
 * @file URotationMovementComponent.cpp
 * @brief Game과 PIE 월드에서 지정한 이동 대상을 피벗 중심으로 회전합니다.
 * @ingroup Engine
 */

#include "Engine/Component/URotationMovementComponent.h"
#include "Core/Literals/UnitLiterals.h"
#include "Engine/Component/USceneComponent.h"
#include "Engine/UWorld.h"

using namespace Core::Literals;

namespace Engine {

    /**
     * @brief 기본 설정으로 URotationMovementComponent 객체를 초기화합니다.
     */
    Engine::URotationMovementComponent::URotationMovementComponent()
		: mRotationRate{0_deg, 90_degps * 1_s, 0_deg} {
    }

    /**
     * @brief 객체 수명을 끝내고 소유한 자원을 정리합니다.
     */
    Engine::URotationMovementComponent::~URotationMovementComponent() = default;

    /**
     * @brief Game 또는 PIE의 실행 중 이동 대상을 회전하고 피벗의 월드 위치를 유지합니다.
     */
    void Engine::URotationMovementComponent::Tick(float DeltaTime) {
        const Engine::UWorld* World{GetWorld()};
        const Core::FTime StepTime{Core::FTime::FromBase(DeltaTime)};

        if (World == nullptr || (World->GetWorldType() != Engine::EWorldType::Game && World->GetWorldType() != Engine::EWorldType::PIE) || !HasBegunPlay() || ShouldSkipUpdate(StepTime)) {
            return;
        }

        const FQuat OldRotation{GetUpdatedComponent()->GetTransform().mRotation};
        const auto Pitch{(mRotationRate.pitch * 1_degps) * StepTime};
        const auto Yaw{(mRotationRate.yaw * 1_degps) * StepTime};
        const auto Roll{(mRotationRate.roll * 1_degps) * StepTime};
        const FQuat DeltaRotation{FRotator{Pitch, Yaw, Roll}.Normalized().Quaternion()};
        const FQuat NewRotation{(mRotationInLocalSpace ? OldRotation * DeltaRotation : DeltaRotation * OldRotation).Normalized()};

        const FVector OldPivot{OldRotation.RotateVector(mPivotTranslation)};
        const FVector NewPivot{NewRotation.RotateVector(mPivotTranslation)};
        MoveUpdatedComponent(OldPivot - NewPivot, NewRotation, false);
    }

    /**
     * @brief 축별 초당 회전 속도를 반환합니다.
     */
    const FRotator& Engine::URotationMovementComponent::GetRotationRate() const {
        return mRotationRate;
    }

    /**
     * @brief 유한한 Pitch·Yaw·Roll 회전 속도를 설정합니다.
     */
    bool Engine::URotationMovementComponent::SetRotationRate(const FRotator& Rate) {
        if (!Rate.IsFinite()) {
            return false;
        }

        mRotationRate = Rate;

        return true;
    }

    /**
     * @brief 회전 증분을 로컬 축으로 합성하는지 조회합니다.
     */
    bool Engine::URotationMovementComponent::GetRotationInLocalSpace() const {
        return mRotationInLocalSpace;
    }

    /**
     * @brief 회전 증분을 로컬 축 또는 월드 축으로 합성하도록 설정합니다.
     */
    bool Engine::URotationMovementComponent::SetRotationInLocalSpace(bool LocalSpace) {
        mRotationInLocalSpace = LocalSpace;

        return true;
    }

    const FVector& Engine::URotationMovementComponent::GetPivotTranslation() const {
        return mPivotTranslation;
    }

    bool Engine::URotationMovementComponent::SetPivotTranslation(const FVector& Translation) {
        if (!Translation.IsFinite()) {
            return false;
        }

        mPivotTranslation = Translation;

        return true;
    }

}
