#include "Engine/AParticleSystemActor.h"
#include "Engine/Component/UParticleSystemComponent.h"
#include "CoreUObject/Reflection/NewObject.h"

#include <utility>

namespace Engine {

    Engine::AParticleSystemActor::AParticleSystemActor() = default;

    Engine::AParticleSystemActor::~AParticleSystemActor() = default;

    bool Engine::AParticleSystemActor::InitializeDefaultComponents() {
        if (!GetComponents().empty()) {
            return false;
        }

        auto Component{CoreUObject::NewObject<Engine::UParticleSystemComponent>()};

        Component->SetName("ParticleSystemComponent");

        return AddComponent(std::move(Component));
    }

    Engine::UParticleSystemComponent* Engine::AParticleSystemActor::GetParticleSystemComponent() const {
        return static_cast<Engine::UParticleSystemComponent*>(FindDefaultComponent(Engine::UParticleSystemComponent::StaticClass(), Core::FName{"ParticleSystemComponent"}));
    }

}
