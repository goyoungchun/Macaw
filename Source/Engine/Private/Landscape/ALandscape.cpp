#include "Engine/Landscape/ALandscape.h"
#include "Engine/Landscape/FLandscapeComponentSceneProxy.h"

#include "CoreUObject/Reflection/Cast.h"
#include "CoreUObject/Reflection/NewObject.h"
#include "Engine/Component/USceneComponent.h"
#include "Engine/UWorld.h"

#include <algorithm>
#include <bit>
#include <string>
#include <utility>

namespace Engine {

    Core::TArray<CoreUObject::TObjectRef<Engine::ALandscapeStreamingProxy>> ALandscape::GetStreamingProxies() const {
        const Engine::UWorld* World{ GetWorld() };
        const Engine::FLandscapeInfo* Info{ World != nullptr && !IsPendingDestroy() ? World->FindLandscapeInfo(*this) : nullptr };

        if (Info == nullptr) {
            return {};
        }

        Core::TArray<CoreUObject::TObjectRef<Engine::ALandscapeStreamingProxy>> Proxies{};
        Proxies.reserve(Info->GetStreamingProxies().size());

        for (const auto& ProxyRef : Info->GetStreamingProxies()) {
            if (const auto* Proxy{ ProxyRef.Get() }; Proxy != nullptr && !Proxy->IsPendingDestroy()) {
                Proxies.push_back(ProxyRef);
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

        // 기존 프록시를 교체하기 전에 공유 격자를 확보해 생성에 실패해도 기존 지형을 보존합니다.
        const auto RenderResources{Engine::FLandscapeRenderResources::Acquire(static_cast<Uint32>(GetQuadsPerComponentSide()))};
        if (RenderResources == nullptr) {
            Error = "Could not build Landscape render grid. The limit is 1024 quads per component side.";

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

    void ALandscape::OnActorTransformChanged(const FTransform& PreviousTransform) {
        const FTransform CurrentTransform{ GetActorTransform() };

        if (mSynchronizingProxies || PreviousTransform.Equals(CurrentTransform, 0.0f)) {
            return;
        }

        const auto Proxies{ GetStreamingProxies() };

        if (Proxies.empty()) {
            return;
        }

        Core::TArray<const Engine::AActor*> AttachAncestors{};
        const Engine::USceneComponent* Parent{ GetRootComponent()->GetAttachParent() };

        while (Parent != nullptr) {
            const Engine::AActor* Owner{ Parent->GetOwner() };
            const Engine::USceneComponent* Root{ Owner != nullptr ? Owner->GetRootComponent() : nullptr };
            AttachAncestors.push_back(Owner);
            Parent = Root != nullptr ? Root->GetAttachParent() : nullptr;
        }

        mSynchronizingProxies = true;

        for (const auto& ProxyRef : Proxies) {
            auto* Proxy{ ProxyRef.Get() };
            const Engine::USceneComponent* Root{ Proxy != nullptr ? Proxy->GetRootComponent() : nullptr };

            if (Root == nullptr || Root->GetAttachParent() != nullptr || std::find(AttachAncestors.begin(), AttachAncestors.end(), Proxy) != AttachAncestors.end()) {
                continue;
            }

            const FTransform ProxyTransform{ Root->GetTransform() };
            Proxy->SetActorTransform(ProxyTransform.Equals(PreviousTransform, 0.0f) ? CurrentTransform : ProxyTransform.GetRelativeTransform(PreviousTransform) * CurrentTransform);
        }

        mSynchronizingProxies = false;
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
                Component->SetQuadsPerSide(static_cast<Uint32>(GetQuadsPerComponentSide()));

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
