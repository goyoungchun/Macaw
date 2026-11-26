#pragma once

#include "Core/CoreMinimal.h"
#include "Core/Types.h"

#include <cmath>
#include <numbers>

namespace BasicGeometry {
    namespace SkyDome {
        inline constexpr Uint32 LongitudeSegments{64};
        inline constexpr Uint32 LatitudeSegments{32};

        inline constexpr float Radius{1.0f};

        inline constexpr Uint32 VertexCount{(LatitudeSegments + 1) * (LongitudeSegments + 1)};
        inline constexpr Uint32 IndexCount{LatitudeSegments * LongitudeSegments * 6};

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
