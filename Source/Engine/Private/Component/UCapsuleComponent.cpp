/**
 * @file UCapsuleComponent.cpp
 * @brief Z축 중심 선분과 반구를 가진 캡슐 충돌 컴포넌트입니다.
 * @ingroup Engine
 */

#include "Engine/Component/UCapsuleComponent.h"
#include "Engine/FPrimitiveDrawInterface.h"
#include "PhysicsRuntime/FKAggregateGeom.h"
#include "Core/Archive.h"
#include "Core/Literals.h"

#include <algorithm>

using namespace Core::Literals;

namespace Engine {

    Engine::UCapsuleComponent::UCapsuleComponent()
		: mCapsuleHalfHeight{1_m},
		  mCapsuleRadius{0.5_m} {
    }

    void Engine::UCapsuleComponent::DrawShape(Engine::FPrimitiveDrawInterface& DrawInterface, const FVector& Color, float Opacity) const {
        const FVector Center{GetTransform().mLocation};
        const FQuat& Rot{GetTransform().mRotation};
        const FVector F{Rot.ForwardVector()};
        const FVector Rt{Rot.RightVector()};
        const FVector Up{Rot.UpVector()};
        const Core::FLength Radius{GetScaledCapsuleRadius()};
        const float AxisHalfLength{std::max(0_m, GetScaledCapsuleHalfHeightWithoutHemisphere()).GetBase()};
        const FVector A{Center - Up * AxisHalfLength};
        const FVector B{Center + Up * AxisHalfLength};

        DrawInterface.DrawArc(A, F, Rt, Radius, 0_rad, 1_turn, Color, true, 1.0f, Opacity);
        DrawInterface.DrawArc(B, F, Rt, Radius, 0_rad, 1_turn, Color, true, 1.0f, Opacity);

        const FVector ForwardOffset{F * Radius.GetBase()};
        const FVector RightOffset{Rt * Radius.GetBase()};

        DrawInterface.DrawLine(A + ForwardOffset, B + ForwardOffset, Color, true, 1.0f, Opacity);
        DrawInterface.DrawLine(A - ForwardOffset, B - ForwardOffset, Color, true, 1.0f, Opacity);
        DrawInterface.DrawLine(A + RightOffset, B + RightOffset, Color, true, 1.0f, Opacity);
        DrawInterface.DrawLine(A - RightOffset, B - RightOffset, Color, true, 1.0f, Opacity);

        DrawInterface.DrawArc(B, F, Up, Radius, 0_rad, 0.5_turn, Color, true, 1.0f, Opacity);
        DrawInterface.DrawArc(B, Rt, Up, Radius, 0_rad, 0.5_turn, Color, true, 1.0f, Opacity);
        DrawInterface.DrawArc(A, F, -Up, Radius, 0_rad, 0.5_turn, Color, true, 1.0f, Opacity);
        DrawInterface.DrawArc(A, Rt, -Up, Radius, 0_rad, 0.5_turn, Color, true, 1.0f, Opacity);
    }

    /**
     * @brief 캡슐 반지름과 반높이로 캡슐 형상 하나를 채웁니다.
     */
    void Engine::UCapsuleComponent::UpdateBodySetup(PhysicsRuntime::FKAggregateGeom& Geometry) const {
        PhysicsRuntime::FKSphylElem& Sphyl{Geometry.mSphylElems.emplace_back()};
        Sphyl.mRadius = mCapsuleRadius;
        // 캡슐 형상의 길이는 반구를 뺀 원기둥 부분의 전체 길이입니다.
        Sphyl.mLength = std::max(0_m, mCapsuleHalfHeight - mCapsuleRadius) * 2.0f;
    }

    /**
     * @brief 충돌 형상의 로컬 축 정렬 경계를 계산합니다.
     */
    FBox Engine::UCapsuleComponent::CalcLocalBounds() const {
        const float Radius{mCapsuleRadius.GetBase()};
        const float HalfHeight{mCapsuleHalfHeight.GetBase()};

        return FBox{FVector{-Radius, -Radius, -HalfHeight}, FVector{Radius, Radius, HalfHeight}};
    }

    /**
     * @brief 아카이브 모드에 따라 값을 읽거나 저장합니다.
     */
    void Engine::UCapsuleComponent::Serialize(Core::FArchive& Archive) {
        if (Archive.IsLoading() && !Archive.HasError() && Archive.HasField("CapsuleRadius")) {
            SetCapsuleRadius(0_m);
        }

        Super::Serialize(Archive);
    }

    /**
     * @brief 캡슐의 로컬 반지름과 반높이를 함께 설정합니다.
     */
    void Engine::UCapsuleComponent::SetCapsuleSize(Core::FLength InRadius, Core::FLength InHalfHeight) {
        const Core::FLength Radius{std::max(0_m, InRadius)};
        const Core::FLength HalfHeight{std::max({0_m, InHalfHeight, InRadius})};

        if (mCapsuleRadius != Radius || mCapsuleHalfHeight != HalfHeight) {
            mCapsuleRadius = Radius;
            mCapsuleHalfHeight = HalfHeight;
            MarkBodySetupDirty();
        }
    }

    /**
     * @brief 월드 스케일을 반영한 캡슐 반지름을 반환합니다.
     */
    Core::FLength Engine::UCapsuleComponent::GetScaledCapsuleRadius() const {
        return mCapsuleRadius * GetShapeScale();
    }

    /**
     * @brief 월드 스케일을 반영한 캡슐 반높이를 반환합니다.
     */
    Core::FLength Engine::UCapsuleComponent::GetScaledCapsuleHalfHeight() const {
        return mCapsuleHalfHeight * GetShapeScale();
    }

    /**
     * @brief 반구를 제외한 캡슐 중심 선분 반길이를 반환합니다.
     */
    Core::FLength Engine::UCapsuleComponent::GetScaledCapsuleHalfHeightWithoutHemisphere() const {
        return (mCapsuleHalfHeight - mCapsuleRadius) * GetShapeScale();
    }

    /**
     * @brief 반구를 포함한 캡슐 반높이를 반환합니다.
     */
    Core::FLength Engine::UCapsuleComponent::GetCapsuleHalfHeight() const {
        return mCapsuleHalfHeight;
    }

    /**
     * @brief 반구를 포함한 캡슐 반높이를 설정합니다.
     */
    void Engine::UCapsuleComponent::SetCapsuleHalfHeight(Core::FLength InHalfHeight) {
        const Core::FLength HalfHeight{std::max(InHalfHeight, mCapsuleRadius)};

        if (mCapsuleHalfHeight != HalfHeight) {
            mCapsuleHalfHeight = HalfHeight;
            MarkBodySetupDirty();
        }
    }

    /**
     * @brief 캡슐의 로컬 반지름을 반환합니다.
     */
    Core::FLength Engine::UCapsuleComponent::GetCapsuleRadius() const {
        return mCapsuleRadius;
    }

    /**
     * @brief 캡슐의 로컬 반지름을 설정합니다.
     */
    void Engine::UCapsuleComponent::SetCapsuleRadius(Core::FLength InRadius) {
        const Core::FLength Radius{std::clamp(InRadius, 0_m, mCapsuleHalfHeight)};

        if (mCapsuleRadius != Radius) {
            mCapsuleRadius = Radius;
            MarkBodySetupDirty();
        }
    }

}
