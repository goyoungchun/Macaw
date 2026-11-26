/**
 * @file FPhysicsAssetDrawing.cpp
 * @brief 물리 에셋 편집기가 바디 설명서의 충돌 형상을 선으로 그리는 함수를 구현합니다.
 * @ingroup Editor
 */

#include "FPhysicsAssetDrawing.h"

#include "Core/Literals.h"
#include "Core/Math/FQuat.h"
#include "Engine/FPrimitiveDrawInterface.h"
#include "PhysicsRuntime/UBodySetup.h"

#include <algorithm>
#include <utility>

using namespace Core::Literals;

void Editor::PhysicsAssetDrawing::DrawBodySetup(Engine::FPrimitiveDrawInterface& Draw, const PhysicsRuntime::UBodySetup& Setup, const FTransform& BodyWorld, const FVector& Color, float Thickness) {
    const PhysicsRuntime::FKAggregateGeom& Geometry{Setup.GetAggGeom()};

    // 형상 중심은 스케일을 반영한 바디 로컬 위치이므로 바디의 회전과 위치로 월드 위치를 구한다.
    const auto ToWorld{[&BodyWorld](const FVector& Local) {
        return BodyWorld.mLocation + BodyWorld.mRotation.RotateVector(Local);
    }};

    for (const PhysicsRuntime::FKSphereElem& Element : Geometry.mSphereElems) {
        const PhysicsRuntime::FKSphereElem Scaled{Element.GetScaled(BodyWorld.mScale)};
        const FVector Center{ToWorld(Scaled.mCenter)};
        const FVector Forward{BodyWorld.mRotation.ForwardVector()};
        const FVector Right{BodyWorld.mRotation.RightVector()};
        const FVector Up{BodyWorld.mRotation.UpVector()};

        Draw.DrawArc(Center, Forward, Right, Scaled.mRadius, 0_rad, 1_turn, Color, false, Thickness);
        Draw.DrawArc(Center, Forward, Up, Scaled.mRadius, 0_rad, 1_turn, Color, false, Thickness);
        Draw.DrawArc(Center, Right, Up, Scaled.mRadius, 0_rad, 1_turn, Color, false, Thickness);
    }

    for (const PhysicsRuntime::FKBoxElem& Element : Geometry.mBoxElems) {
        const PhysicsRuntime::FKBoxElem Scaled{Element.GetScaled(BodyWorld.mScale)};

        Draw.DrawOrientedBox(ToWorld(Scaled.mCenter), BodyWorld.mRotation * Scaled.mRotation.Quaternion(), Scaled.mHalfExtent, Color, false, Thickness);
    }

    for (const PhysicsRuntime::FKSphylElem& Element : Geometry.mSphylElems) {
        const PhysicsRuntime::FKSphylElem Scaled{Element.GetScaled(BodyWorld.mScale)};
        const FQuat Rotation{BodyWorld.mRotation * Scaled.mRotation.Quaternion()};
        const FVector Center{ToWorld(Scaled.mCenter)};
        const FVector Forward{Rotation.ForwardVector()};
        const FVector Right{Rotation.RightVector()};
        const FVector Up{Rotation.UpVector()};
        const Core::FLength Radius{Scaled.mRadius};
        const FVector ForwardOffset{Forward * Radius.GetBase()};
        const FVector RightOffset{Right * Radius.GetBase()};

        // 캡슐 축은 형상 로컬 Z이며 길이는 양 끝 반구 중심 사이의 거리다.
        const FVector HalfLengthOffset{Up * (Scaled.mLength.GetBase() * 0.5f)};
        const FVector Bottom{Center - HalfLengthOffset};
        const FVector Top{Center + HalfLengthOffset};

        Draw.DrawArc(Bottom, Forward, Right, Radius, 0_rad, 1_turn, Color, false, Thickness);
        Draw.DrawArc(Top, Forward, Right, Radius, 0_rad, 1_turn, Color, false, Thickness);
        Draw.DrawLine(Bottom + ForwardOffset, Top + ForwardOffset, Color, false, Thickness);
        Draw.DrawLine(Bottom - ForwardOffset, Top - ForwardOffset, Color, false, Thickness);
        Draw.DrawLine(Bottom + RightOffset, Top + RightOffset, Color, false, Thickness);
        Draw.DrawLine(Bottom - RightOffset, Top - RightOffset, Color, false, Thickness);
        Draw.DrawArc(Top, Forward, Up, Radius, 0_rad, 0.5_turn, Color, false, Thickness);
        Draw.DrawArc(Top, Right, Up, Radius, 0_rad, 0.5_turn, Color, false, Thickness);
        Draw.DrawArc(Bottom, Forward, -Up, Radius, 0_rad, 0.5_turn, Color, false, Thickness);
        Draw.DrawArc(Bottom, Right, -Up, Radius, 0_rad, 0.5_turn, Color, false, Thickness);
    }

    for (const PhysicsRuntime::FKConvexElem& Element : Geometry.mConvexElems) {
        const PhysicsRuntime::FKConvexElem Scaled{Element.GetScaled(BodyWorld.mScale)};

        if (!Scaled.IsValid()) {
            continue;
        }

        // 이웃한 삼각형이 공유하는 모서리는 한 번만 그린다.
        Core::TArray<std::pair<Uint32, Uint32>> Edges{};
        Edges.reserve(Scaled.mIndices.size());

        for (Index_t Triangle{}; Triangle + 2 < Scaled.mIndices.size(); Triangle += 3) {
            for (Index_t Corner{}; Corner < 3; ++Corner) {
                const Uint32 First{Scaled.mIndices[Triangle + Corner]};
                const Uint32 Second{Scaled.mIndices[Triangle + (Corner + 1) % 3]};

                Edges.emplace_back(std::min(First, Second), std::max(First, Second));
            }
        }

        std::sort(Edges.begin(), Edges.end());
        Edges.erase(std::unique(Edges.begin(), Edges.end()), Edges.end());

        for (const auto& [First, Second] : Edges) {
            Draw.DrawLine(ToWorld(Scaled.mVertices[First]), ToWorld(Scaled.mVertices[Second]), Color, false, Thickness);
        }
    }
}
