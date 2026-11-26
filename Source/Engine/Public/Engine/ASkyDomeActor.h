#pragma once

#include "Engine/AActor.h"
#include "ASkyDomeActor.generated.h"

namespace Engine {

    UCLASS()
    class ASkyDomeActor final : public Engine::AActor {
        GENERATED_BODY()

    public:
        ASkyDomeActor() = default;
        ~ASkyDomeActor() override = default;

    public:
        bool InitializeDefaultComponents() override;
    };

}
