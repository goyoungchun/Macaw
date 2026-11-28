#pragma once

#include "CoreUObject/TObjectRef.h"
#include "Engine/Landscape/ALandscapeProxy.h"
#include "Engine/Landscape/ALandscapeStreamingProxy.h"
#include "ALandscape.generated.h"

namespace Engine {

    UCLASS()
    class ALandscape final : public Engine::ALandscapeProxy {
        GENERATED_BODY()

    public:
        Core::TArray<CoreUObject::TObjectRef<Engine::ALandscapeStreamingProxy>> GetStreamingProxies() const;
    };

}
