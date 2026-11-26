/**
 * @file USphereComponent.cpp
 * @brief 로컬 반지름으로 정의한 구 충돌 컴포넌트입니다.
 * @ingroup Engine
 */

#include "Engine/Component/USphereComponent.h"
#include "Engine/FPrimitiveDrawInterface.h"
#include "PhysicsRuntime/FKAggregateGeom.h"
#include "Core/Literals.h"

#include <algorithm>

using namespace Core::Literals;

namespace Engine {

    Engine::USphereComponent::USphereComponent()
		: mSphereRadius{0.5_m} {
    }

    void Engine::USphereComponent::DrawShape(Engine::FPrimitiveDrawInterface& DrawInterface, const FVector& Color, float Opacity) const {
        const FVector Center{GetTransform().mLocation};
        const FQuat& Rot{GetTransform().mRotation};
        const FVector F{Rot.ForwardVector()};
        const FVector Rt{Rot.RightVector()};
        const FVector Up{Rot.UpVector()};
        const Core::FLength Radius{GetScaledSphereRadius()};

        DrawInterface.DrawArc(Center, F, Rt, Radius, 0_rad, 1_turn, Color, true, 1.0f, Opacity);
        DrawInterface.DrawArc(Center, F, Up, Radius, 0_rad, 1_turn, Color, true, 1.0f, Opacity);
        DrawInterface.DrawArc(Center, Rt, Up, Radius, 0_rad, 1_turn, Color, true, 1.0f, Opacity);
    }

    /**
     * @brief 구 반지름으로 구 형상 하나를 채웁니다.
     */
    void Engine::USphereComponent::UpdateBodySetup(PhysicsRuntime::FKAggregateGeom& Geometry) const {
        Geometry.mSphereElems.emplace_back().mRadius = mSphereRadius;
    }

    /**
     * @brief 충돌 형상의 로컬 축 정렬 경계를 계산합니다.
     */
    FBox Engine::USphereComponent::CalcLocalBounds() const {
        const float Radius{mSphereRadius.GetBase()};

        return FBox{FVector{-Radius, -Radius, -Radius}, FVector{Radius, Radius, Radius}};
    }

    /**
     * @brief 구의 로컬 반지름을 설정합니다.
     */
    void Engine::USphereComponent::SetSphereRadius(Core::FLength InRadius) {
        const Core::FLength Radius{std::max(0_m, InRadius)};

        if (mSphereRadius != Radius) {
            mSphereRadius = Radius;
            MarkBodySetupDirty();
        }
    }

    /**
     * @brief 월드 스케일을 반영한 구 반지름을 반환합니다.
     */
    Core::FLength Engine::USphereComponent::GetScaledSphereRadius() const {
        return mSphereRadius * GetShapeScale();
    }

    /**
     * @brief 구의 로컬 반지름을 반환합니다.
     */
    Core::FLength Engine::USphereComponent::GetSphereRadius() const {
        return mSphereRadius;
    }

}
