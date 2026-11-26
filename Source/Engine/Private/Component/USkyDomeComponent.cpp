#include "Engine/Component/USkyDomeComponent.h"

#include "Asset/FAssetManager.h"
#include "Asset/UTexture2D.h"
#include "Core/Base/ErrorHandler.h"
#include "Core/FilePath.h"
#include "Engine/FSceneInterface.h"
#include "Engine/FSkyDomeSceneProxy.h"
#include "Engine/UWorld.h"

namespace Engine {

    Engine::USkyDomeComponent::~USkyDomeComponent() {
        UnregisterComponent();
    }

    const Core::FString& Engine::USkyDomeComponent::GetPanoramaPath() const {
        return mPanoramaPath;
    }

    bool Engine::USkyDomeComponent::SetPanoramaPath(const Core::FString& Path) {
        if (Path.find('\0') != Core::FString::npos) {
            return false;
        }

        const std::filesystem::path AssetPath{Core::FilePath::FromUtf8(Path).lexically_normal()};

        if (!Path.empty() && (Core::FilePath::GetContentMount(AssetPath) == Core::FilePath::EContentMount::Invalid || !AssetPath.has_filename() || AssetPath == L".")) {
            return false;
        }

        const Core::FString PanoramaPath{Core::FilePath::ToGenericUtf8(AssetPath)};
        Asset::UTexture2D* Panorama{LoadPanorama(PanoramaPath)};

        if (!PanoramaPath.empty() && Panorama == nullptr && GetWorld() != nullptr && GetWorld()->GetAssets() != nullptr) {
            return false;
        }

        mPanoramaPath = PanoramaPath;
        mPanorama = Panorama;
        SynchronizeSkyDome();

        return true;
    }

    bool Engine::USkyDomeComponent::SetTransform(const FTransform& Transform) {
        if (!Engine::USceneComponent::SetTransform(Transform)) {
            return false;
        }

        SynchronizeSkyDome();

        return true;
    }

    void Engine::USkyDomeComponent::UnregisterComponent() {
        if (GetWorld() != nullptr && GetWorld()->GetScene() != nullptr) {
            GetWorld()->GetScene()->RemoveSkyDome(GetGuid());
        }

        mPanorama = nullptr;
        Engine::USceneComponent::UnregisterComponent();
    }

    void Engine::USkyDomeComponent::ResolvePanorama() {
        mPanorama = LoadPanorama(mPanoramaPath);
        SynchronizeSkyDome();
    }

    void Engine::USkyDomeComponent::SynchronizeSkyDome() {
        Engine::FSceneInterface* Scene{GetWorld() != nullptr ? GetWorld()->GetScene() : nullptr};

        if (Scene != nullptr) {
            Scene->AddSkyDome({GetGuid(), GetTransform().mRotation, mPanorama, IsOwnerVisible()});
        }
    }

    void Engine::USkyDomeComponent::OnRegister() {
        ResolvePanorama();
    }

    void Engine::USkyDomeComponent::OnOwnerVisibilityChanged() {
        SynchronizeSkyDome();
    }

    Asset::UTexture2D* Engine::USkyDomeComponent::LoadPanorama(const Core::FString& Path) const {
        Asset::FAssetManager* Assets{GetWorld() != nullptr ? GetWorld()->GetAssets() : nullptr};

        if (Assets == nullptr || Path.empty()) {
            return nullptr;
        }

        Core::FString Error{};
        Asset::UTexture2D* Panorama{Assets->LoadTexture(Core::FilePath::FromUtf8(Path), Error)};

        if (Panorama == nullptr) {
            Core::ErrorHandler::Report("Sky Dome", "Cannot load panorama '" + Path + "': " + Error, Core::ErrorHandler::EErrorLevel::Warning);
        }

        return Panorama;
    }

}
