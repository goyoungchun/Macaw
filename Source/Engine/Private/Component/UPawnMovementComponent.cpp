/**
 * @file UPawnMovementComponent.cpp
 * @brief 이동 대상의 소유권을 확인하고 입력을 Pawn에 전달합니다.
 * @ingroup Engine
 */

#include "Engine/Component/UPawnMovementComponent.h"
#include "CoreUObject/Reflection/Cast.h"
#include "Engine/APawn.h"
#include "Engine/Component/USceneComponent.h"

namespace Engine {

    void UPawnMovementComponent::SetUpdatedComponent(Engine::USceneComponent* Component) {
        if (Component == nullptr || CanUseUpdatedComponent(*Component)) {
            Engine::UMovementComponent::SetUpdatedComponent(Component);
        }
    }

    bool UPawnMovementComponent::CanUseUpdatedComponent(const Engine::USceneComponent& Component) const {
        const Engine::APawn* Pawn{GetPawnOwner()};

        return Pawn != nullptr && Component.GetOwner() == Pawn;
    }

    Engine::APawn* UPawnMovementComponent::GetPawnOwner() const {
        return CoreUObject::Cast<Engine::APawn>(GetOwner());
    }

    void UPawnMovementComponent::AddInputVector(const FVector& WorldVector) {
        if (Engine::APawn* Pawn{GetPawnOwner()}) {
            Pawn->AddMovementInput(WorldVector);
        }
    }

    FVector UPawnMovementComponent::GetPendingInputVector() const {
        const Engine::APawn* Pawn{GetPawnOwner()};

        return Pawn != nullptr ? Pawn->GetPendingMovementInputVector() : FVector{};
    }

    FVector UPawnMovementComponent::GetLastInputVector() const {
        const Engine::APawn* Pawn{GetPawnOwner()};

        return Pawn != nullptr ? Pawn->GetLastMovementInputVector() : FVector{};
    }

    FVector UPawnMovementComponent::ConsumeInputVector() {
        Engine::APawn* Pawn{GetPawnOwner()};

        return Pawn != nullptr ? Pawn->ConsumeMovementInputVector() : FVector{};
    }

}
