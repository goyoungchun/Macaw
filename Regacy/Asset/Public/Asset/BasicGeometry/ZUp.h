#pragma once

#include "Core/CoreMinimal.h"
#include "Core/Types.h"

#include <cstddef>

namespace BasicGeometry {
    // Rotate the former Y-up mesh basis +90 degrees around X while preserving winding.
    template <std::size_t Count>
    constexpr Core::TStaticArray<FVector3, Count> ToZUp(const Core::TStaticArray<FVector3, Count>& Vectors) {
        Core::TStaticArray<FVector3, Count> Result{};

        for (std::size_t Index{0}; Index < Count; ++Index) {
            Result[Index] = FVector3{Vectors[Index].mX, -Vectors[Index].mZ, Vectors[Index].mY};
        }

        return Result;
    }
}
