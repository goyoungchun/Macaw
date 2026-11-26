#include "Engine/ASkyDomeActor.h"

#include "CoreUObject/Reflection/NewObject.h"
#include "Engine/Component/USkyDomeComponent.h"

namespace Engine {

    bool Engine::ASkyDomeActor::InitializeDefaultComponents() {
        if (!GetComponents().empty()) {
            return false;
        }

        auto Component{CoreUObject::NewObject<Engine::USkyDomeComponent>()};
        Component->SetName("SkyDomeComponent");

        return AddComponent(std::move(Component));
    }

}
