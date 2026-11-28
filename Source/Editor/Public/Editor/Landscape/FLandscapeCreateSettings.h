#pragma once

#include "Core/Math/FVector.h"
#include "Engine/Landscape/ALandscape.h"

namespace Editor {

    struct FLandscapeCreateSettings {
        FVector mLocation{};
        Int32 mComponentCountX{8};
        Int32 mComponentCountY{8};
        Int32 mProxySizeInComponents{4};
        Engine::ELandscapeSectionsPerComponent mSectionsPerComponent{Engine::ELandscapeSectionsPerComponent::One};
        Int32 mQuadsPerSection{63};
    };
}
