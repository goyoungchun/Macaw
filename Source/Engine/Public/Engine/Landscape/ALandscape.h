#pragma once

#include "CoreUObject/TObjectRef.h"
#include "CoreUObject/Reflection/TNativeEnum.h"
#include "Engine/Landscape/ALandscapeProxy.h"
#include "Engine/Landscape/ALandscapeStreamingProxy.h"
#include "ALandscape.generated.h"

namespace Engine {

    UENUM()
    enum class ELandscapeSectionsPerComponent : Uint8 {
        One = 1,
        Four = 4
    };

    UCLASS()
    class ALandscape final : public Engine::ALandscapeProxy {
        GENERATED_BODY()

    public:
        Core::TArray<CoreUObject::TObjectRef<Engine::ALandscapeStreamingProxy>> GetStreamingProxies() const;
        bool BuildLandscape(Core::FString& Error);

        Int32 GetComponentCountX() const;
        bool SetComponentCountX(Int32 Count);
        Int32 GetComponentCountY() const;
        bool SetComponentCountY(Int32 Count);
        Int32 GetProxySizeInComponents() const;
        bool SetProxySizeInComponents(Int32 Size);

        Engine::ELandscapeSectionsPerComponent GetSectionsPerComponent() const;
        bool SetSectionsPerComponent(Engine::ELandscapeSectionsPerComponent Sections);
        Int32 GetQuadsPerSection() const;
        bool SetQuadsPerSection(Int32 Quads);

        Uint64 GetSizeXInQuads() const;
        Uint64 GetSizeYInQuads() const;

    private:
        bool InitializeStreamingProxy(Engine::ALandscapeStreamingProxy& Proxy, Int32 StartX, Int32 StartY, Int32 SizeX, Int32 SizeY, const Core::TArray<Engine::FLandscapeSection>& Sections);
        Uint64 GetQuadsPerComponentSide() const;

    private:
        UPROPERTY(Edit, Serialize, Category = "Landscape", Getter = GetComponentCountX, Setter = SetComponentCountX)
        Int32 mComponentCountX{ 8 };

        UPROPERTY(Edit, Serialize, Category = "Landscape", Getter = GetComponentCountY, Setter = SetComponentCountY)
        Int32 mComponentCountY{ 8 };

        UPROPERTY(Edit, Serialize, Category = "Landscape", Getter = GetProxySizeInComponents, Setter = SetProxySizeInComponents)
        Int32 mProxySizeInComponents{ 4 };

        UPROPERTY(Edit, Serialize, Category = "Landscape", Getter = GetSectionsPerComponent, Setter = SetSectionsPerComponent)
        Engine::ELandscapeSectionsPerComponent mSectionsPerComponent{ Engine::ELandscapeSectionsPerComponent::One };

        UPROPERTY(Edit, Serialize, Category = "Landscape", Getter = GetQuadsPerSection, Setter = SetQuadsPerSection)
        Int32 mQuadsPerSection{ 63 };
    };

}
