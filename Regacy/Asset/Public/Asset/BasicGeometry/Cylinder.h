#pragma once

#include "Core/CoreMinimal.h"
#include "Core/Types.h"

#include <cmath>
#include <numbers>

namespace BasicGeometry {
    namespace Cylinder {
        inline constexpr Uint32 Segments{32};
        inline constexpr float Radius{0.5f};
        inline constexpr float HalfHeight{0.5f};

        inline constexpr Uint32 SideVertexCount{(Segments + 1) * 2};
        inline constexpr Uint32 CapVertexCount{Segments + 2};
        inline constexpr Uint32 VertexCount{SideVertexCount + CapVertexCount * 2};
        inline constexpr Uint32 IndexCount{Segments * 12};

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
