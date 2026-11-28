#include "Engine/Landscape/ALandscape.h"

#include "CoreUObject/Reflection/Cast.h"
#include "Engine/ULevel.h"
#include "Engine/UWorld.h"

namespace Engine {

    Core::TArray<CoreUObject::TObjectRef<Engine::ALandscapeStreamingProxy>> ALandscape::GetStreamingProxies() const {
        const Engine::UWorld* World{ GetWorld() };
        const Engine::ULevel* Level{ World != nullptr ? World->GetPersistentLevel() : nullptr };

        if (Level == nullptr || IsPendingDestroy()) {
            return {};
        }

        Core::TArray<CoreUObject::TObjectRef<Engine::ALandscapeStreamingProxy>> Proxies{};

        for (const auto& Actor : Level->GetActors()) {
            if (auto* Proxy{ CoreUObject::Cast<Engine::ALandscapeStreamingProxy>(Actor.get()) }; Proxy != nullptr && Proxy->GetWorld() == World && !Proxy->IsPendingDestroy() && Proxy->GetLandscapeActor().Get() == this) {
                Proxies.push_back(CoreUObject::MakeObjectRef(Proxy));
            }
        }

        return Proxies;
    }

}
