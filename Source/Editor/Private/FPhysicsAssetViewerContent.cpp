/**
 * @file FPhysicsAssetViewerContent.cpp
 * @brief 물리 에셋 편집기가 스켈레탈 메시와 물리 장면을 담아 두는 미리보기 뷰어 콘텐츠를 구현합니다.
 * @ingroup Editor
 */

#include "FPhysicsAssetViewerContent.h"

#include "Asset/FAssetManager.h"
#include "Asset/USkeletalMesh.h"
#include "CoreUObject/FObjectRegistry.h"
#include "CoreUObject/Reflection/Cast.h"
#include "Engine/ASkeletalMeshActor.h"
#include "Engine/Component/USkeletalMeshComponent.h"
#include "Engine/FSceneInterface.h"
#include "Engine/UWorld.h"
#include "PhysicsRuntime/FPhysicsRuntime.h"
#include "RenderCore/IRendererModule.h"
#include "Viewer/FPreviewScene.h"

#include <d3d11.h>

using namespace Editor;

FPhysicsAssetViewerContent::FPhysicsAssetViewerContent(RenderCore::IRendererModule* Renderer, Asset::FAssetManager* Assets)
    : mRenderer{Renderer},
      mAssets{Assets} {
}

FPhysicsAssetViewerContent::~FPhysicsAssetViewerContent() = default;

bool FPhysicsAssetViewerContent::SetMesh(Asset::USkeletalMesh* Mesh, Asset::USkeleton* Skeleton, PhysicsRuntime::FPhysicsRuntime* Runtime, Core::FString& Error) {
    Error.clear();

    if (Mesh == nullptr || Skeleton == nullptr) {
        Error = "A skeletal mesh and a compatible skeleton are required.";

        return false;
    }

    auto Scene{std::make_unique<Viewer::FPreviewScene>(mRenderer, mAssets)};

    if (!Scene->Initialize(Error)) {
        return false;
    }

    Engine::UWorld* World{Scene->GetWorld()};
    auto* Actor{CoreUObject::Cast<Engine::ASkeletalMeshActor>(World->SpawnActor(Engine::ASkeletalMeshActor::StaticClass()))};
    Engine::USkeletalMeshComponent* Component{Actor != nullptr ? Actor->GetSkeletalMeshComponent() : nullptr};

    if (Component == nullptr || !Component->SetPreviewAssets(Mesh, Skeleton) || Component->GetSkeletalMesh() == nullptr || Component->GetSkeleton() == nullptr || !Component->GetPose().GetRevision().IsValid()) {
        Error = "Cannot prepare this mesh and skeleton. Select a compatible skeleton.";

        return false;
    }

    if (mRenderer != nullptr && !Component->GetSkeletalMesh()->CreateResources(static_cast<ID3D11Device*>(mRenderer->GetNativeDevice()))) {
        Error = "Cannot create the skeletal mesh preview resources.";

        return false;
    }

    Component->SetApplyRootMotionToActor(false);

    // 월드에 물리 장면이 생길 때 이미 등록된 컴포넌트가 물리 에셋의 바디와 관절을 만들므로 컴포넌트를 먼저 준비합니다.
    if (Runtime != nullptr) {
        World->SetPhysicsRuntime(Runtime);
        World->CreatePhysicsScene();
    }

    mScene = std::move(Scene);
    mComponent = Component;
    SynchronizeScene();

    return true;
}

Engine::UWorld* FPhysicsAssetViewerContent::GetWorld() const {
    return mScene != nullptr ? mScene->GetWorld() : nullptr;
}

Engine::USkeletalMeshComponent* FPhysicsAssetViewerContent::GetComponent() const {
    const Engine::UWorld* World{GetWorld()};
    Engine::USkeletalMeshComponent* Component{mComponent.Get()};

    return World != nullptr && World->ContainsObject(Component) ? Component : nullptr;
}

bool FPhysicsAssetViewerContent::HasPhysicsScene() const {
    const Engine::UWorld* World{GetWorld()};

    return World != nullptr && World->GetPhysicsScene() != nullptr;
}

bool FPhysicsAssetViewerContent::IsReady() const {
    return GetComponent() != nullptr;
}

const FBox& FPhysicsAssetViewerContent::GetBounds() const {
    return mBounds;
}

Engine::FSceneInterface* FPhysicsAssetViewerContent::GetScene() const {
    return mScene != nullptr ? mScene->GetScene() : nullptr;
}

void FPhysicsAssetViewerContent::PrepareEnvironment(Engine::FSceneViewFamily& Family) {
    const Engine::USkeletalMeshComponent* Component{GetComponent()};
    const Asset::USkeletalMesh* Mesh{Component != nullptr ? Component->GetSkeletalMesh() : nullptr};

    if (mScene != nullptr && Mesh != nullptr) {
        mScene->PrepareEnvironment(Family, Mesh->GetBounds());
    }
}

void FPhysicsAssetViewerContent::Synchronize() {
    SynchronizeScene();
}

void FPhysicsAssetViewerContent::SynchronizeScene() {
    Engine::USkeletalMeshComponent* Component{GetComponent()};
    Engine::UWorld* PreviewWorld{GetWorld()};

    if (Component == nullptr || Component->GetSkeletalMesh() == nullptr || PreviewWorld == nullptr) {
        return;
    }

    const FMatrix World{Component->GetWorldMatrix()};
    mBounds = Component->GetSkeletalMesh()->CalcSkinnedWorldBounds(Component->GetPose().GetSkinMatrices(), World);

    PreviewWorld->SendAllEndOfFrameUpdates();

    if (Engine::FSceneInterface* Scene{PreviewWorld->GetScene()}) {
        Scene->UpdatePrimitiveTransform(Component->GetGuid(), World, mBounds);
    }
}
