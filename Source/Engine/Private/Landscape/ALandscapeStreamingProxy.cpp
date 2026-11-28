#include "Engine/Landscape/ALandscapeStreamingProxy.h"

namespace Engine {

    CoreUObject::TObjectRef<Engine::ALandscape> ALandscapeStreamingProxy::GetLandscapeActor() const {
        return mLandscapeActorRef;
    }

    void ALandscapeStreamingProxy::SetLandscapeActor(CoreUObject::TObjectRef<Engine::ALandscape> LandscapeActor) {
        mLandscapeActorRef = LandscapeActor;
    }

}
