#include "Engine/Particles/ParticleAssets.h"
#include "Engine/Particles/UParticleSystem.h"
#include "Asset/FAssetManager.h"
#include "CoreUObject/Reflection/Cast.h"

namespace Engine {

    bool RegisterParticleAssetTypes(Asset::FAssetManager& Assets) {
        Asset::FAssetTypeHandler Handler{};

        Handler.mClass = Engine::UParticleSystem::StaticClass();
        Handler.mDefaultName = "ParticleSystem";
        Handler.mInitialize = [](CoreUObject::UObject& Object) {
            static_cast<Engine::UParticleSystem&>(Object).InitializeDefault();
        };
        // 캐시된 에셋은 바꾸지 않으므로 분포 굽기와 Payload 배치를 여기서 한 번만 합니다.
        Handler.mPostLoad = [](CoreUObject::UObject& Object, Core::FString&) {
            static_cast<Engine::UParticleSystem&>(Object).BuildEmitters();

            return true;
        };

        return Assets.RegisterAssetType(Asset::EAssetType::ParticleSystem, std::move(Handler));
    }

    Engine::UParticleSystem* LoadParticleSystem(Asset::FAssetManager& Assets, const std::filesystem::path& Path, Core::FString& Error) {
        return CoreUObject::Cast<Engine::UParticleSystem>(Assets.LoadRegisteredAsset(Path, Asset::EAssetType::ParticleSystem, Error));
    }

    Engine::UParticleSystem* CreateParticleSystem(Asset::FAssetManager& Assets, const std::filesystem::path& Directory, const Core::FString& Name, std::filesystem::path& Path, Core::FString& Error) {
        return CoreUObject::Cast<Engine::UParticleSystem>(Assets.CreateRegisteredAsset(Directory, Name, Asset::EAssetType::ParticleSystem, Path, Error));
    }

}
