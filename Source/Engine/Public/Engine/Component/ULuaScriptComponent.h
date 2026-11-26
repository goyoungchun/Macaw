#pragma once

#include "Engine/Component/UActorComponent.h"
#include <filesystem>
#include "ULuaScriptComponent.generated.h"

namespace Engine {

    class FLuaRuntime;
    class FLuaScriptInstance;

    UCLASS()
    class ULuaScriptComponent final : public Engine::UActorComponent {
        GENERATED_BODY()

    public:
        ULuaScriptComponent();
        ~ULuaScriptComponent() override;

    public:
        void BeginPlay() override;
        void Tick(float DeltaTime) override;
        void EndPlay() override;

        const Core::FString& GetScriptPath() const;
        bool SetScriptPath(const Core::FString& Path);
        std::filesystem::path ResolveScriptPath() const;

        bool IsEnabled() const;
        bool SetEnabled(bool Enabled);

    private:
        friend class FLuaRuntime;

    private:
        UPROPERTY(Edit, Serialize, Category = "Script", Getter = GetScriptPath, Setter = SetScriptPath)
        Core::FString mScriptPath{};

        UPROPERTY(Edit, Serialize, Category = "Script", Getter = IsEnabled, Setter = SetEnabled)
        bool mEnabled{true};

        Engine::FLuaScriptInstance* mRuntimeInstance{};
    };

}
