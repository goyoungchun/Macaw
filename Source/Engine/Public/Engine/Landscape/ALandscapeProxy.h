#pragma once

#include "CoreUObject/TObjectRef.h"
#include "Engine/APartitionActor.h"
#include "Engine/Landscape/ULandscapeComponent.h"
#include "ALandscapeProxy.generated.h"

namespace Engine {

    UCLASS(Abstract)
    class ALandscapeProxy : public Engine::APartitionActor {
        GENERATED_BODY()

    public:
        bool InitializeDefaultComponents() override;

        Core::TArray<CoreUObject::TObjectRef<Engine::ULandscapeComponent>> GetLandscapeComponents() const;
    };

}
