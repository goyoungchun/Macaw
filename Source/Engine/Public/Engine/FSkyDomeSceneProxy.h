#pragma once

#include "Core/Guid.h"
#include "Core/Math/FQuat.h"
#include "CoreUObject/TObjectRef.h"

namespace Asset {
    class UTexture2D;
}

namespace Engine {

    struct FSkyDomeSceneProxy {
        Core::FGuid mId{};
        FQuat mRotation{};
        CoreUObject::TObjectRef<Asset::UTexture2D> mPanorama{};
        bool mVisible{};
    };

}
