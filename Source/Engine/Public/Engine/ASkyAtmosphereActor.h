#pragma once

#include "Engine/AActor.h"
#include "ASkyAtmosphereActor.generated.h"

namespace Engine {

    UCLASS()
    class ASkyAtmosphereActor final : public Engine::AActor {
        GENERATED_BODY()

    public:
        ASkyAtmosphereActor() = default;
        ~ASkyAtmosphereActor() override = default;

    public:
        bool InitializeDefaultComponents() override;
    };

}
