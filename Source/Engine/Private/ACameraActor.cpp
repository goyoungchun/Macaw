/**
 * @file ACameraActor.cpp
 * @brief 카메라 컴포넌트를 갖고 이동·회전 입력을 처리하는 액터에 대한 구현입니다.
 * @ingroup Engine
 */

#include "Engine/ACameraActor.h"
#include "Core/Literals/AngleFunctions.h"
#include "Engine/Component/UCameraComponent.h"
#include "Core/Input.h"
#include "Engine/UWorld.h"
#include "Core/Math/FRotator.h"
#include "CoreUObject/Reflection/NewObject.h"
#include <algorithm>
#include <cmath>
#include <utility>

using namespace Core::Literals;

namespace Engine {

    /**
     * @brief 액터의 기본 컴포넌트를 생성하고 루트 컴포넌트를 정합니다.
     */
    bool Engine::ACameraActor::InitializeDefaultComponents() {
        if (!GetComponents().empty()) {
            return false;
        }

        auto Camera{CoreUObject::NewObject<Engine::UCameraComponent>()};
        Camera->SetName("CameraComponent");

        return AddComponent(std::move(Camera));
    }

    /**
     * @brief 이 액터에 소속된 카메라 컴포넌트를 조회합니다.
     */
    Engine::UCameraComponent* Engine::ACameraActor::GetCameraComponent() const {
        return static_cast<Engine::UCameraComponent*>(FindDefaultComponent(Engine::UCameraComponent::StaticClass(), Core::FName{"CameraComponent"}));
    }

    /**
     * @brief 뷰포트 입력을 받아 프레임 시간에 따라 액터의 동작을 갱신합니다.
     */
    void Engine::ACameraActor::Input(const Core::FInput& Input, float DeltaTime) {
        using Core::EInputRoute;
        using Core::EKey;

        Engine::UCameraComponent* Camera{GetCameraComponent()};
        const FVector2 MouseDelta{Input.GetMouseDelta(EInputRoute::Game)};

        if (Camera == nullptr || GetWorld() == nullptr || GetWorld()->GetPlayState() != Engine::EWorldPlayState::Playing || !HasBegunPlay() || IsPendingDestroy() || !Input.IsGameInputEnabled() || !std::isfinite(DeltaTime) || DeltaTime < 0.0f || !std::isfinite(MouseDelta.x) || !std::isfinite(MouseDelta.y)) {
            return;
        }

        const FTransform Previous{Camera->GetTransform()};
        FTransform Transform{Previous};
        FRotator Rotation{Transform.mRotation.Rotator()};

        if (Input.IsDown(EKey::MouseRight, EInputRoute::Game)) {
            Rotation.yaw += MouseDelta.x * Core::Math::ToDegrees(0.2_deg);
            Rotation.pitch = std::clamp(Rotation.pitch - MouseDelta.y * Core::Math::ToDegrees(0.2_deg), Core::Math::ToDegrees(-89_deg), Core::Math::ToDegrees(89_deg));
            Transform.mRotation = Rotation.Quaternion();
        }

        const FVector Local{static_cast<float>(Input.IsDown(EKey::W, EInputRoute::Game)) - static_cast<float>(Input.IsDown(EKey::S, EInputRoute::Game)), static_cast<float>(Input.IsDown(EKey::D, EInputRoute::Game)) - static_cast<float>(Input.IsDown(EKey::A, EInputRoute::Game)), 0.0f};
        FVector Movement{Transform.mRotation * Local};
        Movement.z += static_cast<float>(Input.IsDown(EKey::E, EInputRoute::Game)) - static_cast<float>(Input.IsDown(EKey::Q, EInputRoute::Game));
        const Core::FSpeed MoveSpeed{Input.IsDown(EKey::Shift, EInputRoute::Game) ? 12_mps : 4_mps};
        Transform.mLocation += Movement.Normalized() * DeltaTime * MoveSpeed.GetBase();

        if (Input.IsDown(EKey::MouseRight, EInputRoute::Game) || Movement.LengthSquared() > 0.0f) {
            if (GetRootComponent() == nullptr || Camera == GetRootComponent()) {
                Camera->SetTransform(Transform);

                return;
            }

            FTransform RootTransform{GetActorTransform()};
            const FQuat RotationDelta{Transform.mRotation * Previous.mRotation.Inverse()};
            RootTransform.mLocation = Transform.mLocation + RotationDelta * (RootTransform.mLocation - Previous.mLocation);
            RootTransform.mRotation = (RotationDelta * RootTransform.mRotation).Normalized();
            SetActorTransform(RootTransform);
        }
    }

}
