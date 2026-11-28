#include "Engine/Landscape/ALandscape.h"

#include "CoreUObject/Reflection/Cast.h"
#include "CoreUObject/Reflection/NewObject.h"
#include "Engine/ULevel.h"
#include "Engine/UWorld.h"

#include <algorithm>
#include <bit>
#include <string>
#include <utility>

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

    bool ALandscape::BuildLandscape(Core::FString& Error) {
        Error.clear();
        Engine::UWorld* World{ GetWorld() };

        if (World == nullptr || IsPendingDestroy() || World->GetPlayState() != Engine::EWorldPlayState::Stopped || World->IsDispatching()) {
            Error = "Landscape must be in a stopped world to build its regions.";

            return false;
        }

        constexpr Uint64 MaxComponents{ 16384 };
        const Uint64 ComponentCount{ static_cast<Uint64>(mComponentCountX) * static_cast<Uint64>(mComponentCountY) };

        if (ComponentCount > MaxComponents) {
            Error = "Landscape exceeds the build limit of " + std::to_string(MaxComponents) + " components.";

            return false;
        }

        Core::TArray<Engine::FLandscapeSection> Sections{ { 0, 0 } };

        if (mSectionsPerComponent == Engine::ELandscapeSectionsPerComponent::Four) {
            Sections.emplace_back(1, 0);
            Sections.emplace_back(0, 1);
            Sections.emplace_back(1, 1);
        }

        const auto ExistingProxies{ GetStreamingProxies() };
        Core::TArray<CoreUObject::TObjectRef<Engine::ALandscapeStreamingProxy>> NewProxies{};
        Engine::UWorld::FDispatchScope Dispatch{ World };
        const auto DestroyProxies{ [World](const Core::TArray<CoreUObject::TObjectRef<Engine::ALandscapeStreamingProxy>>& Proxies) {
            for (const auto& Proxy : Proxies) {
                World->DestroyActor(Proxy);
            }
        } };

        for (Int32 Y{}; Y < mComponentCountY; Y += mProxySizeInComponents) {
            const Int32 SizeY{ std::min(mProxySizeInComponents, mComponentCountY - Y) };

            for (Int32 X{}; X < mComponentCountX; X += mProxySizeInComponents) {
                const Int32 SizeX{ std::min(mProxySizeInComponents, mComponentCountX - X) };
                auto* Proxy{ CoreUObject::Cast<Engine::ALandscapeStreamingProxy>(World->SpawnActor(Engine::ALandscapeStreamingProxy::StaticClass())) };

                if (Proxy != nullptr) {
                    NewProxies.push_back(CoreUObject::MakeObjectRef(Proxy));

                    if (InitializeStreamingProxy(*Proxy, X, Y, SizeX, SizeY, Sections)) {
                        continue;
                    }
                }

                DestroyProxies(NewProxies);
                Error = "Could not build Landscape region at tile (" + std::to_string(X) + ", " + std::to_string(Y) + ").";

                return false;
            }
        }

        DestroyProxies(ExistingProxies);

        return true;
    }

    Int32 ALandscape::GetComponentCountX() const {
        return mComponentCountX;
    }

    bool ALandscape::SetComponentCountX(Int32 Count) {
        if (Count <= 0) {
            return false;
        }

        mComponentCountX = Count;

        return true;
    }

    Int32 ALandscape::GetComponentCountY() const {
        return mComponentCountY;
    }

    bool ALandscape::SetComponentCountY(Int32 Count) {
        if (Count <= 0) {
            return false;
        }

        mComponentCountY = Count;

        return true;
    }

    Int32 ALandscape::GetProxySizeInComponents() const {
        return mProxySizeInComponents;
    }

    bool ALandscape::SetProxySizeInComponents(Int32 Size) {
        if (Size <= 0) {
            return false;
        }

        mProxySizeInComponents = Size;

        return true;
    }

    Engine::ELandscapeSectionsPerComponent ALandscape::GetSectionsPerComponent() const {
        return mSectionsPerComponent;
    }

    bool ALandscape::SetSectionsPerComponent(Engine::ELandscapeSectionsPerComponent Sections) {
        if (Sections != Engine::ELandscapeSectionsPerComponent::One && Sections != Engine::ELandscapeSectionsPerComponent::Four) {
            return false;
        }

        mSectionsPerComponent = Sections;

        return true;
    }

    Int32 ALandscape::GetQuadsPerSection() const {
        return mQuadsPerSection;
    }

    bool ALandscape::SetQuadsPerSection(Int32 Quads) {
        if (Quads <= 0 || !std::has_single_bit(static_cast<Uint32>(Quads) + Uint32{1})) {
            return false;
        }

        mQuadsPerSection = Quads;

        return true;
    }

    Uint64 ALandscape::GetSizeXInQuads() const {
        return static_cast<Uint64>(mComponentCountX) * GetQuadsPerComponentSide();
    }

    Uint64 ALandscape::GetSizeYInQuads() const {
        return static_cast<Uint64>(mComponentCountY) * GetQuadsPerComponentSide();
    }

    bool ALandscape::InitializeStreamingProxy(Engine::ALandscapeStreamingProxy& Proxy, Int32 StartX, Int32 StartY, Int32 SizeX, Int32 SizeY, const Core::TArray<Engine::FLandscapeSection>& Sections) {
        if (!Proxy.SetRegionStartX(StartX) || !Proxy.SetRegionStartY(StartY) || !Proxy.SetRegionSizeX(SizeX) || !Proxy.SetRegionSizeY(SizeY) || !Proxy.SetActorTransform(GetActorTransform())) {
            return false;
        }

        Proxy.SetLandscapeActor(CoreUObject::MakeObjectRef(this));

        for (Int32 Y{ StartY }; Y < StartY + SizeY; ++Y) {
            for (Int32 X{ StartX }; X < StartX + SizeX; ++X) {
                auto Component{ CoreUObject::NewObject<Engine::ULandscapeComponent>() };
                Component->SetSections(Sections);

                if (!Component->SetName("LandscapeComponent_" + std::to_string(X) + "_" + std::to_string(Y)) || !Component->SetTileX(X) || !Component->SetTileY(Y) || !Proxy.AddComponent(std::move(Component))) {
                    return false;
                }
            }
        }

        return true;
    }

    Uint64 ALandscape::GetQuadsPerComponentSide() const {
        const Uint64 SectionsPerSide{ mSectionsPerComponent == Engine::ELandscapeSectionsPerComponent::Four ? 2u : 1u };

        return static_cast<Uint64>(mQuadsPerSection) * SectionsPerSide;
    }

}
