#pragma once

#include "Engine/APartitionActor.h"
#include "ALandscapeProxy.generated.h"

namespace Engine {

    class ULandscapeComponent;

    UCLASS(Abstract)
    class ALandscapeProxy : public Engine::APartitionActor {
        GENERATED_BODY()

    public:
        bool InitializeDefaultComponents() override;

        Core::TArray<Engine::ULandscapeComponent*> GetLandscapeComponents() const;
    };

}
