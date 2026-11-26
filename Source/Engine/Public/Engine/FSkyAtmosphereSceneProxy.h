#pragma once

#include "Core/Guid.h"
#include "Core/Literals/UnitLiterals.h"
#include "Core/Math/FVector.h"

namespace Engine {

    struct FSkyAtmosphereSceneProxy {
        Core::FGuid mId{};
        FVector mGroundPosition{};
        bool mVisible{};
        Core::FLength mGroundRadius{};
        Core::FLength mAtmosphereHeight{};
        Core::FLength mRayleighScaleHeight{};
        float mRayleighScatteringScale{};
        Core::FLength mMieScaleHeight{};
        float mMieScatteringScale{};
        float mMieAnisotropy{};
        float mOzoneAbsorptionScale{};
    };

}
