#pragma once

#include "CoreUObject/TObjectRef.h"
#include "Engine/Landscape/ALandscapeProxy.h"
#include "ALandscapeStreamingProxy.generated.h"

namespace Engine {

    class ALandscape;

    UCLASS()
    class ALandscapeStreamingProxy final : public Engine::ALandscapeProxy {
        GENERATED_BODY()

    public:
        CoreUObject::TObjectRef<Engine::ALandscape> GetLandscapeActor() const;
        void SetLandscapeActor(CoreUObject::TObjectRef<Engine::ALandscape> LandscapeActor);

        CoreUObject::TObjectRef<Engine::AActor> GetSceneOutlinerParent() const override;

        Int32 GetRegionStartX() const;
        bool SetRegionStartX(Int32 X);
        Int32 GetRegionStartY() const;
        bool SetRegionStartY(Int32 Y);

        Int32 GetRegionSizeX() const;
        bool SetRegionSizeX(Int32 X);
        Int32 GetRegionSizeY() const;
        bool SetRegionSizeY(Int32 Y);

    private:
        UPROPERTY(Serialize)
        CoreUObject::TObjectRef<Engine::ALandscape> mLandscapeActorRef{};

        UPROPERTY(Edit, Serialize, Category = "Landscape", Getter = GetRegionStartX, Setter = SetRegionStartX)
        Int32 mRegionStartX{};

        UPROPERTY(Edit, Serialize, Category = "Landscape", Getter = GetRegionStartY, Setter = SetRegionStartY)
        Int32 mRegionStartY{};

        UPROPERTY(Edit, Serialize, Category = "Landscape", Getter = GetRegionSizeX, Setter = SetRegionSizeX)
        Int32 mRegionSizeX{ 1 };

        UPROPERTY(Edit, Serialize, Category = "Landscape", Getter = GetRegionSizeY, Setter = SetRegionSizeY)
        Int32 mRegionSizeY{ 1 };
    };

}
