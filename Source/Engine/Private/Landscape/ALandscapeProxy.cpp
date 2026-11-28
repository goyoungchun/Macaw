#include "Engine/Landscape/ALandscapeProxy.h"

#include "CoreUObject/Reflection/Cast.h"
#include "CoreUObject/Reflection/NewObject.h"
#include "Engine/Component/USceneComponent.h"

#include <utility>

namespace Engine {

    bool ALandscapeProxy::InitializeDefaultComponents() {
        if (!GetComponents().empty()) {
            return false;
        }

        auto Root{ CoreUObject::NewObject<Engine::USceneComponent>() };
        Root->SetName("SceneComponent");

        return AddComponent(std::move(Root));
    }

    Core::TArray<CoreUObject::TObjectRef<Engine::ULandscapeComponent>> ALandscapeProxy::GetLandscapeComponents() const {
        Core::TArray<CoreUObject::TObjectRef<Engine::ULandscapeComponent>> LandscapeComponents{};

        for (const auto& Component : GetComponents()) {
            if (auto* LandscapeComponent{ CoreUObject::Cast<Engine::ULandscapeComponent>(Component.get()) }; LandscapeComponent != nullptr) {
                LandscapeComponents.push_back(CoreUObject::MakeObjectRef(LandscapeComponent));
            }
        }

        return LandscapeComponents;
    }

}
