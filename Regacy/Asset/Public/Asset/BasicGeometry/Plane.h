#pragma once

#include "Core/CoreMinimal.h"
#include "Core/Types.h"

#include "Asset/BasicGeometry/ZUp.h"

namespace BasicGeometry {
    namespace Plane {
        inline const Core::TStaticArray<FVector3, 4> Positions{ToZUp(Core::TStaticArray<FVector3, 4>{FVector3{-0.5f, 0.0f, -0.5f}, FVector3{-0.5f, 0.0f, 0.5f}, FVector3{0.5f, 0.0f, 0.5f}, FVector3{0.5f, 0.0f, -0.5f}})};

        inline const Core::TStaticArray<FVector3, 4> Normals{ToZUp(Core::TStaticArray<FVector3, 4>{FVector3{0.0f, 1.0f, 0.0f}, FVector3{0.0f, 1.0f, 0.0f}, FVector3{0.0f, 1.0f, 0.0f}, FVector3{0.0f, 1.0f, 0.0f}})};

        inline const Core::TStaticArray<FVector2D, 4> TexCoords{FVector2D{0.0f, 1.0f}, FVector2D{0.0f, 0.0f}, FVector2D{1.0f, 0.0f}, FVector2D{1.0f, 1.0f}};

        inline constexpr Core::TStaticArray<Uint32, 6> Indices{0, 1, 2, 0, 2, 3};
    }
}
