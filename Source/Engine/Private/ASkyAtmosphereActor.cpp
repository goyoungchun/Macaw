#include "Engine/ASkyAtmosphereActor.h"

#include "CoreUObject/Reflection/NewObject.h"
#include "Engine/Component/USkyAtmosphereComponent.h"

namespace Engine {

    bool Engine::ASkyAtmosphereActor::InitializeDefaultComponents() {
        if (!GetComponents().empty()) {
            return false;
        }

        auto Component{CoreUObject::NewObject<Engine::USkyAtmosphereComponent>()};
        Component->SetName("SkyAtmosphereComponent");

        return AddComponent(std::move(Component));
    }

}
