#include "Engine/Landscape/ALandscapeStreamingProxy.h"

#include "Engine/Landscape/ALandscape.h"

namespace Engine {

    Engine::ALandscape* ALandscapeStreamingProxy::GetLandscapeActor() const {
        return mLandscapeActorRef.Get();
    }

    void ALandscapeStreamingProxy::SetLandscapeActor(Engine::ALandscape* LandscapeActor) {
        mLandscapeActorRef = CoreUObject::MakeObjectRef(LandscapeActor);
    }

}
