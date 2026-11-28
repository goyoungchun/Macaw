#pragma once

#include "Core/ReflectionMacros.h"
#include "Core/Types.h"
#include "FLandscapeSection.generated.h"

namespace Engine {

    USTRUCT()
    struct FLandscapeSection {
        GENERATED_BODY()

    public:
        FLandscapeSection() = default;
        FLandscapeSection(Uint32 X, Uint32 Y);

    public:
        Uint32 GetX() const;
        Uint32 GetY() const;

    private:
        UPROPERTY(Serialize)
        Uint32 mX{};

        UPROPERTY(Serialize)
        Uint32 mY{};
    };

}
