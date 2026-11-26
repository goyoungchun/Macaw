/**
 * @file UPawnMovementComponent.h
 * @brief 소유 Pawn의 이동 대상과 입력에 접근합니다.
 * @ingroup Engine
 */

#pragma once

#include "Engine/Component/UMovementComponent.h"
#include "UPawnMovementComponent.generated.h"

namespace Engine {

    class APawn;

    /** @brief Pawn 입력을 전달하고 소유 Pawn의 공간 컴포넌트를 이동 대상으로 사용합니다. */
    UCLASS()
    class UPawnMovementComponent : public Engine::UMovementComponent {
        GENERATED_BODY()

    public:
        /** @brief 소유 Pawn의 컴포넌트를 지정하거나 nullptr로 이동 대상을 해제합니다. */
        void SetUpdatedComponent(Engine::USceneComponent* Component) override;

        /** @brief 이 컴포넌트를 소유한 Pawn을 반환합니다. */
        Engine::APawn* GetPawnOwner() const;

        /** @brief 월드 공간의 이동 입력을 소유 Pawn에 누적합니다. */
        void AddInputVector(const FVector& WorldVector);

        FVector GetPendingInputVector() const;
        FVector GetLastInputVector() const;
        FVector ConsumeInputVector();

    protected:
        bool CanUseUpdatedComponent(const Engine::USceneComponent& Component) const override;
    };

}
