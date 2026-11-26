/**
 * @file FKAggregateGeom.cpp
 * @brief 하나의 바디를 구성하는 단순 충돌 형상 묶음입니다.
 * @ingroup PhysicsRuntime
 */

#include "Core/Types.h"
#include "Core/Math/FMatrix.h"
#include "PhysicsRuntime/FKAggregateGeom.h"

namespace PhysicsRuntime {

    /**
     * @brief 포함한 모든 형상의 개수를 반환합니다.
     */
    Index_t PhysicsRuntime::FKAggregateGeom::GetElementCount() const {
        return mSphereElems.size() + mBoxElems.size() + mSphylElems.size() + mConvexElems.size();
    }

    /**
     * @brief 포함한 형상이 없는지 검사합니다.
     */
    bool PhysicsRuntime::FKAggregateGeom::IsEmpty() const {
        return GetElementCount() == 0;
    }

    std::optional<FBox> FKAggregateGeom::CalcAABB(const FMatrix& LocalToWorld) const {
        if (IsEmpty() || !LocalToWorld.IsFinite()) {
            return {};
        }

        std::optional<FBox> Bounds{};
        const auto Include{[&Bounds](const FBox& ShapeBounds) {
            if (!ShapeBounds.mMinimum.IsFinite() || !ShapeBounds.mMaximum.IsFinite()) {
                return;
            }

            if (Bounds.has_value()) {
                Bounds->mMinimum = Bounds->mMinimum.Min(ShapeBounds.mMinimum);
                Bounds->mMaximum = Bounds->mMaximum.Max(ShapeBounds.mMaximum);
            } else {
                Bounds = ShapeBounds;
            }
        }};

        const FVector UnitSphereExtent{
            FVector{LocalToWorld(0, 0), LocalToWorld(1, 0), LocalToWorld(2, 0)}.Length(),
            FVector{LocalToWorld(0, 1), LocalToWorld(1, 1), LocalToWorld(2, 1)}.Length(),
            FVector{LocalToWorld(0, 2), LocalToWorld(1, 2), LocalToWorld(2, 2)}.Length()
        };

        for (const FKSphereElem& Sphere : mSphereElems) {
            if (!Sphere.mRadius.IsFinite() || Sphere.mRadius <= Core::FLength{}) {
                continue;
            }

            const FVector Center{LocalToWorld.TransformPosition(Sphere.mCenter)};
            const FVector Extent{UnitSphereExtent * Sphere.mRadius.GetBase()};
            Include(FBox{Center - Extent, Center + Extent});
        }

        for (const FKBoxElem& Box : mBoxElems) {
            if (!Box.mHalfExtent.IsFinite() || Box.mHalfExtent.x <= 0.0f || Box.mHalfExtent.y <= 0.0f || Box.mHalfExtent.z <= 0.0f || !Box.mRotation.IsFinite()) {
                continue;
            }

            const FMatrix ShapeToWorld{FMatrix::Rotation(Box.mRotation) * FMatrix::Translation(Box.mCenter) * LocalToWorld};
            Include(FBox{-Box.mHalfExtent, Box.mHalfExtent}.Transformed(ShapeToWorld));
        }

        for (const FKSphylElem& Capsule : mSphylElems) {
            if (!Capsule.mRadius.IsFinite() || Capsule.mRadius <= Core::FLength{} || !Capsule.mLength.IsFinite() || Capsule.mLength < Core::FLength{} || !Capsule.mRotation.IsFinite()) {
                continue;
            }

            const FVector Center{LocalToWorld.TransformPosition(Capsule.mCenter)};
            const FVector Axis{LocalToWorld.TransformVector(Capsule.mRotation.Quaternion().UpVector())};
            const FVector Extent{Axis.Abs() * (Capsule.mLength.GetBase() * 0.5f) + UnitSphereExtent * Capsule.mRadius.GetBase()};
            Include(FBox{Center - Extent, Center + Extent});
        }

        for (const FKConvexElem& Convex : mConvexElems) {
            if (!Convex.IsValid()) {
                continue;
            }

            for (const FVector& Vertex : Convex.mVertices) {
                const FVector Position{LocalToWorld.TransformPosition(Vertex)};
                Include(FBox{Position, Position});
            }
        }

        return Bounds;
    }

}
