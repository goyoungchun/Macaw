#pragma once

#include "CoreUObject/TObjectRef.h"
#include "Engine/Landscape/ALandscapeProxy.h"
#include "ALandscapeStreamingProxy.generated.h"

namespace Engine {

    class ALandscape;

    UCLASS(NoFactory)
    class ALandscapeStreamingProxy final : public Engine::ALandscapeProxy {
        GENERATED_BODY()

    public:
        CoreUObject::TObjectRef<Engine::ALandscape> GetLandscapeActor() const;
        void SetLandscapeActor(CoreUObject::TObjectRef<Engine::ALandscape> LandscapeActor);

    private:
        UPROPERTY(Serialize)
        CoreUObject::TObjectRef<Engine::ALandscape> mLandscapeActorRef{};
    };

}
