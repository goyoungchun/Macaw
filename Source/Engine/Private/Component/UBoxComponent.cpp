/**
 * @file UBoxComponent.cpp
 * @brief 로컬 반길이로 정의한 상자 충돌 컴포넌트입니다.
 * @ingroup Engine
 */

#include "Engine/Component/UBoxComponent.h"
#include "Core/Literals/UnitLiterals.h"
#include "Engine/FPrimitiveDrawInterface.h"
#include "PhysicsRuntime/FKAggregateGeom.h"

#include <algorithm>
#include <cmath>

using namespace Core::Literals;

namespace Engine {

    namespace {

        constexpr float HalfExtent{(0.5_m).GetBase()};

    }

    Engine::UBoxComponent::UBoxComponent()
		: mBoxExtent{HalfExtent, HalfExtent, HalfExtent} {
    }

    void Engine::UBoxComponent::DrawShape(Engine::FPrimitiveDrawInterface& DrawInterface, const FVector& Color, float Opacity) const {
        const FVector Center{GetTransform().mLocation};
        const FQuat& Rot{GetTransform().mRotation};
        const FVector Extent{GetScaledBoxExtent()};
        const FVector X{Rot.ForwardVector() * Extent.x};
        const FVector Y{Rot.RightVector() * Extent.y};
        const FVector Z{Rot.UpVector() * Extent.z};

        const FVector V0{Center + X + Y + Z};
        const FVector V1{Center + X - Y + Z};
        const FVector V2{Center + X - Y - Z};
        const FVector V3{Center + X + Y - Z};
        const FVector V4{Center - X + Y + Z};
        const FVector V5{Center - X - Y + Z};
        const FVector V6{Center - X - Y - Z};
        const FVector V7{Center - X + Y - Z};

        DrawInterface.DrawLine(V0, V1, Color, true, 1.0f, Opacity);
        DrawInterface.DrawLine(V1, V2, Color, true, 1.0f, Opacity);
        DrawInterface.DrawLine(V2, V3, Color, true, 1.0f, Opacity);
        DrawInterface.DrawLine(V3, V0, Color, true, 1.0f, Opacity);
        DrawInterface.DrawLine(V4, V5, Color, true, 1.0f, Opacity);
        DrawInterface.DrawLine(V5, V6, Color, true, 1.0f, Opacity);
        DrawInterface.DrawLine(V6, V7, Color, true, 1.0f, Opacity);
        DrawInterface.DrawLine(V7, V4, Color, true, 1.0f, Opacity);
        DrawInterface.DrawLine(V0, V4, Color, true, 1.0f, Opacity);
        DrawInterface.DrawLine(V1, V5, Color, true, 1.0f, Opacity);
        DrawInterface.DrawLine(V2, V6, Color, true, 1.0f, Opacity);
        DrawInterface.DrawLine(V3, V7, Color, true, 1.0f, Opacity);
    }

    /**
     * @brief 상자 반길이로 상자 형상 하나를 채웁니다.
     */
    void Engine::UBoxComponent::UpdateBodySetup(PhysicsRuntime::FKAggregateGeom& Geometry) const {
        Geometry.mBoxElems.emplace_back().mHalfExtent = mBoxExtent;
    }

    /**
     * @brief 충돌 형상의 로컬 축 정렬 경계를 계산합니다.
     */
    FBox Engine::UBoxComponent::CalcLocalBounds() const {
        return FBox{ -mBoxExtent, mBoxExtent };
    }

    /**
     * @brief 상자의 로컬 축별 반길이를 설정합니다.
     */
    void Engine::UBoxComponent::SetBoxExtent(const FVector& InExtent) {
        const FVector Extent{ std::max(InExtent.x, 0.0f), std::max(InExtent.y, 0.0f), std::max(InExtent.z, 0.0f) };

        if (!(mBoxExtent == Extent)) {
            mBoxExtent = Extent;
            MarkBodySetupDirty();
        }
    }

    /**
     * @brief 월드 스케일을 반영한 상자 축별 반길이를 반환합니다.
     */
    FVector Engine::UBoxComponent::GetScaledBoxExtent() const {
        const FVector Scale{GetTransform().mScale};

        return FVector{mBoxExtent.x * std::abs(Scale.x), mBoxExtent.y * std::abs(Scale.y), mBoxExtent.z * std::abs(Scale.z)};
    }

    /**
     * @brief 상자의 로컬 축별 반길이를 반환합니다.
     */
    const FVector& Engine::UBoxComponent::GetBoxExtent() const {
        return mBoxExtent;
    }

}
