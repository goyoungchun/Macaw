/**
 * @file USceneComponent.cpp
 * @brief 검증된 위치, 회전과 스케일을 보관하는 공간 컴포넌트입니다.
 * @ingroup Engine
 */

#include "Engine/Component/USceneComponent.h"
#include "Engine/AActor.h"
#include "Engine/Collision/FHitResult.h"
#include "Core/Reflection/EPropertyFlags.h"
#include "Core/Math/FMatrix.h"
#include "Core/Math/FTransform.h"
#include "CoreUObject/Reflection/Cast.h"

#include <algorithm>

namespace Engine {

    /**
     * @brief USceneComponent의 초기 상태와 전달받은 의존성을 구성합니다.
     */
    Engine::USceneComponent::USceneComponent() = default;

    /**
     * @brief 객체의 수명을 끝내고 소유한 리소스를 정리합니다.
     */
    Engine::USceneComponent::~USceneComponent() {
        SetAttachParent(nullptr);

        for (Engine::USceneComponent* Child : mAttachChildren) {
            Child->mAttachParent = nullptr;
        }
    }

    /**
     * @brief 컴포넌트의 위치·회전·스케일을 반환합니다.
     */
    const FTransform& Engine::USceneComponent::GetTransform() const {
        return mTransform;
    }

    /**
     * @brief 유한하고 유효한 변환을 검증하여 공간 컴포넌트에 적용합니다.
     */
    bool Engine::USceneComponent::SetTransform(const FTransform& Transform) {
        if (!Transform.IsValid()) {
            return false;
        }

        const FTransform Previous{mTransform};
        mTransform = Transform;

        if (const Engine::AActor* Owner{GetOwner()}; Owner != nullptr && Owner->GetRootComponent() == this) {
            for (const auto& Component : Owner->GetComponents()) {
                auto* Scene{CoreUObject::Cast<Engine::USceneComponent>(Component.get())};

                if (Scene != nullptr && Scene != this && Scene->mAttachParent == nullptr) {
                    Scene->ApplyParentTransform(Previous, mTransform);
                }
            }
        }

        for (Engine::USceneComponent* Child : mAttachChildren) {
            Child->ApplyParentTransform(Previous, mTransform);
        }

        return true;
    }

    /**
     * @brief 위치, 회전과 스케일이 적용된 월드 행렬을 반환합니다.
     */
    FMatrix Engine::USceneComponent::GetWorldMatrix() const {
        return mTransform.ToMatrix();
    }

    Engine::USceneComponent* Engine::USceneComponent::GetAttachParent() const {
        return mAttachParent;
    }

    const Engine::USceneComponent* Engine::USceneComponent::GetEffectiveAttachParent() const {
        if (mAttachParent != nullptr) {
            return mAttachParent;
        }

        const Engine::AActor* Owner{GetOwner()};
        const Engine::USceneComponent* Root{Owner != nullptr ? Owner->GetRootComponent() : nullptr};

        return Root != this ? Root : nullptr;
    }

    bool Engine::USceneComponent::CanSetAttachParent(const Engine::USceneComponent* Parent) const {
        if (Parent == mAttachParent) {
            return false;
        }

        if (Parent == nullptr) {
            return true;
        }

        const Engine::AActor* Owner{GetOwner()};
        const Engine::AActor* ParentOwner{Parent->GetOwner()};

        if (Owner == nullptr || ParentOwner == nullptr || Owner->GetWorld() != ParentOwner->GetWorld() || Owner->IsPendingDestroy() || ParentOwner->IsPendingDestroy() || (Owner->GetRootComponent() == this) != (Owner != ParentOwner)) {
            return false;
        }

        for (const Engine::USceneComponent* Ancestor{Parent}; Ancestor != nullptr; Ancestor = Ancestor->GetEffectiveAttachParent()) {
            if (Ancestor == this) {
                return false;
            }
        }

        return true;
    }

    bool Engine::USceneComponent::SetAttachParent(Engine::USceneComponent* Parent) {
        if (!CanSetAttachParent(Parent)) {
            return false;
        }

        UpdateAttachParent(Parent);

        return true;
    }

    void Engine::USceneComponent::UpdateAttachParent(Engine::USceneComponent* Parent) {
        if (Parent != nullptr) {
            Parent->mAttachChildren.push_back(this);
        }

        if (mAttachParent != nullptr) {
            std::erase(mAttachParent->mAttachChildren, this);
        }

        mAttachParent = Parent;
    }

    void Engine::USceneComponent::ApplyParentTransform(const FTransform& Previous, const FTransform& Current) {
        const FTransform Relative{Previous.InverseTransformPosition(mTransform.mLocation), Previous.mRotation.Inverse() * mTransform.mRotation, mTransform.mScale / Previous.mScale};

        SetTransform(Relative * Current);
    }

    void Engine::USceneComponent::DetachExternalAttachments() {
        if (mAttachParent != nullptr && mAttachParent->GetOwner() != GetOwner()) {
            SetAttachParent(nullptr);
        }

        std::erase_if(mAttachChildren, [this](Engine::USceneComponent* Child) {
            if (Child->GetOwner() == GetOwner()) {
                return false;
            }

            Child->mAttachParent = nullptr;

            return true;
        });
    }

    /**
     * @brief 컴포넌트를 월드 변위만큼 옮기고, 요청하면 이동 경로를 쓸어 막히는 지점에서 멈춥니다.
     */
    bool Engine::USceneComponent::MoveComponent(const FVector& Delta, bool Sweep, Engine::FHitResult* OutHit) {
        Engine::FHitResult Hit{};
        const FVector Start{mTransform.mLocation};
        Hit.Init(Start, Start + Delta);

        bool Moved{};

        if (Delta.IsFinite()) {
            Moved = Delta.IsNearlyZero() || MoveComponentImpl(Delta, Sweep, Hit);
        }

        if (OutHit != nullptr) {
            *OutHit = Hit;
        }

        return Moved;
    }

    /**
     * @brief MoveComponent의 실제 이동을 처리합니다. 기본 구현은 쓸지 않고 Delta만큼 옮깁니다.
     */
    bool Engine::USceneComponent::MoveComponentImpl(const FVector& Delta, bool, Engine::FHitResult&) {
        return ApplyMove(Delta);
    }

    /**
     * @brief 위치만 Delta만큼 옮깁니다. 소유 액터의 루트이면 액터 전체를 옮깁니다.
     */
    bool Engine::USceneComponent::ApplyMove(const FVector& Delta) {
        FTransform Transform{mTransform};
        Transform.mLocation += Delta;

        return SetTransform(Transform);
    }

}
