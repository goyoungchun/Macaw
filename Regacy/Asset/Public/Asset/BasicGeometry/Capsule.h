#pragma once

#include "Core/CoreMinimal.h"
#include "Core/Types.h"

#include <cmath>
#include <numbers>

namespace BasicGeometry {
    namespace Capsule {
        inline constexpr Uint32 Segments{32};
        inline constexpr Uint32 HemisphereRings{8};

        inline constexpr float Radius{0.25f};
        inline constexpr float HalfCylinderHeight{0.25f};

        inline constexpr Uint32 RingCount{HemisphereRings * 2 + 2};
        inline constexpr Uint32 VertexCount{RingCount * (Segments + 1)};
        inline constexpr Uint32 IndexCount{(RingCount - 1) * Segments * 6};

        struct FGeometry {
            Core::TStaticArray<FVector3, VertexCount> mPositions{};
            Core::TStaticArray<FVector3, VertexCount> mNormals{};
            Core::TStaticArray<FVector2D, VertexCount> mTexCoords{};
            Core::TStaticArray<Uint32, IndexCount> mIndices{};
        };

        FGeometry GenerateGeometry();

        inline const FGeometry Geometry{GenerateGeometry()};

        inline const auto& Positions{Geometry.mPositions};
        inline const auto& Normals{Geometry.mNormals};
        inline const auto& TexCoords{Geometry.mTexCoords};
        inline const auto& Indices{Geometry.mIndices};
    }
}
