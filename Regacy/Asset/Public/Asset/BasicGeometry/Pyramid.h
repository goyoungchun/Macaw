#pragma once

#include "Core/CoreMinimal.h"
#include "Core/Types.h"

#include "Asset/BasicGeometry/ZUp.h"

namespace BasicGeometry {
    namespace Pyramid {
        inline const Core::TStaticArray<FVector3, 16> Positions{ToZUp(Core::TStaticArray<FVector3, 16>{
            // Front
            FVector3{-0.5f, -0.5f, 0.5f},
            FVector3{0.5f, -0.5f, 0.5f},
            FVector3{0.0f, 0.5f, 0.0f},

            // Right
            FVector3{0.5f, -0.5f, 0.5f},
            FVector3{0.5f, -0.5f, -0.5f},
            FVector3{0.0f, 0.5f, 0.0f},

            // Back
            FVector3{0.5f, -0.5f, -0.5f},
            FVector3{-0.5f, -0.5f, -0.5f},
            FVector3{0.0f, 0.5f, 0.0f},

            // Left
            FVector3{-0.5f, -0.5f, -0.5f},
            FVector3{-0.5f, -0.5f, 0.5f},
            FVector3{0.0f, 0.5f, 0.0f},

            // Bottom
            FVector3{-0.5f, -0.5f, -0.5f},
            FVector3{0.5f, -0.5f, -0.5f},
            FVector3{0.5f, -0.5f, 0.5f},
            FVector3{-0.5f, -0.5f, 0.5f}})};

        inline const Core::TStaticArray<FVector3, 16> Normals{ToZUp(Core::TStaticArray<FVector3, 16>{FVector3{0.0f, 0.4472136f, 0.8944272f}, FVector3{0.0f, 0.4472136f, 0.8944272f}, FVector3{0.0f, 0.4472136f, 0.8944272f}, FVector3{0.8944272f, 0.4472136f, 0.0f}, FVector3{0.8944272f, 0.4472136f, 0.0f}, FVector3{0.8944272f, 0.4472136f, 0.0f}, FVector3{0.0f, 0.4472136f, -0.8944272f}, FVector3{0.0f, 0.4472136f, -0.8944272f}, FVector3{0.0f, 0.4472136f, -0.8944272f}, FVector3{-0.8944272f, 0.4472136f, 0.0f}, FVector3{-0.8944272f, 0.4472136f, 0.0f}, FVector3{-0.8944272f, 0.4472136f, 0.0f}, FVector3{0.0f, -1.0f, 0.0f}, FVector3{0.0f, -1.0f, 0.0f}, FVector3{0.0f, -1.0f, 0.0f}, FVector3{0.0f, -1.0f, 0.0f}})};

        inline const Core::TStaticArray<FVector2D, 16> TexCoords{FVector2D{0.0f, 1.0f}, FVector2D{1.0f, 1.0f}, FVector2D{0.5f, 0.0f}, FVector2D{0.0f, 1.0f}, FVector2D{1.0f, 1.0f}, FVector2D{0.5f, 0.0f}, FVector2D{0.0f, 1.0f}, FVector2D{1.0f, 1.0f}, FVector2D{0.5f, 0.0f}, FVector2D{0.0f, 1.0f}, FVector2D{1.0f, 1.0f}, FVector2D{0.5f, 0.0f}, FVector2D{0.0f, 1.0f}, FVector2D{1.0f, 1.0f}, FVector2D{1.0f, 0.0f}, FVector2D{0.0f, 0.0f}};

        inline constexpr Core::TStaticArray<Uint32, 18> Indices{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 12, 14, 15};
    }
}
