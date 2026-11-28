#include "Engine/Landscape/ALandscapeStreamingProxy.h"
#include "Engine/Landscape/ALandscape.h"

namespace Engine {

    CoreUObject::TObjectRef<Engine::ALandscape> ALandscapeStreamingProxy::GetLandscapeActor() const {
        return mLandscapeActorRef;
    }

    void ALandscapeStreamingProxy::SetLandscapeActor(CoreUObject::TObjectRef<Engine::ALandscape> LandscapeActor) {
        mLandscapeActorRef = LandscapeActor;
    }

    CoreUObject::TObjectRef<Engine::AActor> ALandscapeStreamingProxy::GetSceneOutlinerParent() const {
        const auto LandscapeRef{GetLandscapeActor()};
        const Engine::ALandscape* Landscape{LandscapeRef.Get()};

        if (Landscape != nullptr && Landscape->GetWorld() == GetWorld() && !Landscape->IsPendingDestroy()) {
            return LandscapeRef;
        }

        return Engine::ALandscapeProxy::GetSceneOutlinerParent();
    }

    Int32 ALandscapeStreamingProxy::GetRegionStartX() const {
        return mRegionStartX;
    }

    bool ALandscapeStreamingProxy::SetRegionStartX(Int32 X) {
        if (X < 0) {
            return false;
        }

        mRegionStartX = X;

        return true;
    }

    Int32 ALandscapeStreamingProxy::GetRegionStartY() const {
        return mRegionStartY;
    }

    bool ALandscapeStreamingProxy::SetRegionStartY(Int32 Y) {
        if (Y < 0) {
            return false;
        }

        mRegionStartY = Y;

        return true;
    }

    Int32 ALandscapeStreamingProxy::GetRegionSizeX() const {
        return mRegionSizeX;
    }

    bool ALandscapeStreamingProxy::SetRegionSizeX(Int32 X) {
        if (X <= 0) {
            return false;
        }

        mRegionSizeX = X;

        return true;
    }

    Int32 ALandscapeStreamingProxy::GetRegionSizeY() const {
        return mRegionSizeY;
    }

    bool ALandscapeStreamingProxy::SetRegionSizeY(Int32 Y) {
        if (Y <= 0) {
            return false;
        }

        mRegionSizeY = Y;

        return true;
    }

}
