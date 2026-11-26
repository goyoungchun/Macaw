#pragma once

#include "Core/CoreMinimal.h"
#include "Core/Types.h"

#include <cmath>
#include <numbers>

namespace BasicGeometry {
    namespace Torus {
        inline constexpr Uint32 MajorSegments{32};
        inline constexpr Uint32 MinorSegments{16};

        inline constexpr float MajorRadius{5.0f / 13.0f};
        inline constexpr float MinorRadius{1.5f / 13.0f};

        inline constexpr Uint32 VertexCount{(MajorSegments + 1) * (MinorSegments + 1)};
        inline constexpr Uint32 IndexCount{MajorSegments * MinorSegments * 6};

        struct FGeometry {
            Core::TStaticArray<FVector3, VertexCount> mPositions{};
            Core::TStaticArray<FVector3, VertexCount> mNormals{};
            Core::TStaticArray<FVector2D, VertexCount> mTexCoords{};
            Core::TStaticArray<Uint32, IndexCount> mIndices{};
        };

        FGeometry GenerateGeometry(float InMajorRadius = MajorRadius, float InMinorRadius = MinorRadius);

        inline const FGeometry Geometry{GenerateGeometry()};

        inline const auto& Positions{Geometry.mPositions};
        inline const auto& Normals{Geometry.mNormals};
        inline const auto& TexCoords{Geometry.mTexCoords};
        inline const auto& Indices{Geometry.mIndices};
    }
}
