/**
 * @file FMeshViewerContent.cpp
 * @brief 메시·재질·스켈레톤의 미리보기 장면과 텍스처의 직접 표시를 제공합니다.
 * @ingroup Viewer
 */

#include "Viewer/FMeshViewerContent.h"
#include "Core/Literals/AngleFunctions.h"

#include "Viewer/FBonePreview.h"
#include "Asset/FAssetManager.h"
#include "Asset/UStaticMesh.h"
#include "Asset/USkeletalMesh.h"
#include "Asset/USkeleton.h"
#include "Asset/UMaterial.h"
#include "Asset/UTexture2D.h"
#include "Core/FilePath.h"
#include "Core/Math/FRotator.h"
#include "Core/Math/FTransform.h"
#include "CoreUObject/Reflection/NewObject.h"
#include "CoreUObject/Reflection/Cast.h"
#include "Engine/FSceneInterface.h"
#include "Engine/FPrimitiveSceneProxy.h"
#include "Engine/FPrimitiveDrawInterface.h"
#include "Engine/PrimitiveMesh.h"
#include "Engine/EditorMesh.h"
#include "RenderCore/IRendererModule.h"

#include <algorithm>

using namespace Core::Literals;

using namespace Viewer;

FMeshViewerContent::FMeshViewerContent(RenderCore::IRendererModule* Renderer, Asset::FAssetManager* Assets)
	: mRenderer{Renderer},
	  mAssets{Assets},
	  mScene{Renderer, Assets} {
}

FMeshViewerContent::~FMeshViewerContent() = default;

bool FMeshViewerContent::SetAsset(CoreUObject::UObject* Object, Asset::EAssetType Type, Core::FString& Error) {
    Error.clear();

    if (mRenderer == nullptr || Object == nullptr) {
        Error = "The viewer requires a renderer and an asset";

        return false;
    }

    switch (Type) {
    case Asset::EAssetType::Texture:
        mTexture = CoreUObject::Cast<Asset::UTexture2D>(Object);
        break;
    case Asset::EAssetType::Skeleton:
        mSkeleton = CoreUObject::Cast<Asset::USkeleton>(Object);
        break;
    case Asset::EAssetType::Material:
        mMaterial = CoreUObject::Cast<Asset::UMaterial>(Object);
        break;
    case Asset::EAssetType::SkeletalMesh:
        mSkeletalMesh = CoreUObject::Cast<Asset::USkeletalMesh>(Object);
        mMesh = mSkeletalMesh != nullptr ? mSkeletalMesh->GetGeometry() : nullptr;
        break;
    case Asset::EAssetType::Mesh:
        mMesh = CoreUObject::Cast<Asset::UStaticMesh>(Object);
        break;
    default:
        break;
    }

    if (mTexture == nullptr && mSkeleton == nullptr && mMaterial == nullptr && mMesh == nullptr) {
        Error = "The asset does not match the specified type";

        return false;
    }

    auto* Device{static_cast<ID3D11Device*>(mRenderer->GetNativeDevice())};

    if (mTexture != nullptr) {
        if (!mTexture->CreateResources(Device)) {
            Error = "Cannot create texture preview resources";

            return false;
        }

        return true;
    }

    if (!mScene.Initialize(Error)) {
        return false;
    }

    if (mSkeleton != nullptr) {
        if (mSkeleton->GetGlobalBindPoses().empty()) {
            Error = "The skeleton has no preview bones";

            return false;
        }

        Synchronize();

        return true;
    }

    if (mMaterial != nullptr) {
        return SetMaterialPreviewPrimitive(Engine::EPrimitiveType::Sphere, Error);
    }

    if (mMesh == nullptr || !mMesh->CreateResources(Device)) {
        Error = "Cannot create mesh preview resources";

        return false;
    }

    for (const Asset::FMeshMaterialSlot& Slot : mMesh->GetMeshData().GetMaterialSlots()) {
        Asset::UMaterial* Material{Slot.GetMaterialPath().empty() || mAssets == nullptr ? nullptr : mAssets->LoadMaterial(Core::FilePath::FromUtf8(Slot.GetMaterialPath()), Error)};

        if (!Slot.GetMaterialPath().empty() && Material == nullptr) {
            return false;
        }

        mMaterials.push_back(Material);
    }

    Synchronize();

    return true;
}

bool FMeshViewerContent::SetMaterialPreviewPrimitive(Engine::EPrimitiveType Type, Core::FString& Error) {
    Error.clear();

    if (mMaterial == nullptr || mScene.GetScene() == nullptr || Type >= Engine::EPrimitiveType::Count) {
        Error = "Cannot set the material preview mesh";
        return false;
    }

    auto PrimitiveMesh{CoreUObject::NewObject<Asset::UStaticMesh>()};
    auto* Device{static_cast<ID3D11Device*>(mRenderer->GetNativeDevice())};

    if (!PrimitiveMesh->SetMeshData(Engine::GetPrimitiveMesh(Type)) || !PrimitiveMesh->CreateResources(Device)) {
        Error = "Cannot create material preview mesh resources";
        return false;
    }

    mMaterials.resize(PrimitiveMesh->GetMeshData().GetMaterialSlots().size());
    mPrimitiveMesh = std::move(PrimitiveMesh);
    mMesh = mPrimitiveMesh.get();
    mMaterialPreviewTransform = Type == Engine::EPrimitiveType::Plane ? FTransform{{}, FRotator{90_deg, 0_deg, 0_deg}.Quaternion(), {1.0f, 1.0f, 1.0f}}.ToMatrix() : FMatrix{};
    Refresh();

    return true;
}

bool FMeshViewerContent::IsReady() const {
    return mTexture != nullptr || (mScene.GetScene() != nullptr && (mMesh != nullptr || mSkeleton != nullptr));
}

const FBox& FMeshViewerContent::GetBounds() const {
    return mBounds;
}

Engine::FSceneInterface* FMeshViewerContent::GetScene() const {
    return mScene.GetScene();
}

void FMeshViewerContent::PrepareEnvironment(Engine::FSceneViewFamily& Family) {
    mScene.PrepareEnvironment(Family, mMesh == nullptr || mMaterial != nullptr ? mBounds : mMesh->GetBounds());
}

Asset::UTexture2D* FMeshViewerContent::GetTexture() const {
    return mTexture;
}

void FMeshViewerContent::Synchronize() {
    if (!mDirty) {
        return;
    }

    mDirty = false;

    if (mSkeleton != nullptr && (mMesh == nullptr || mShowBones) && !mSkeleton->GetGlobalBindPoses().empty()) {
        const auto& Transforms{mSkeleton->GetGlobalBindPoses()};
        const FVector First{Transforms.front().TransformPosition({})};
        mBounds = {First, First};

        for (const FMatrix& Transform : Transforms) {
            const FVector Joint{Transform.TransformPosition({})};

            mBounds.mMinimum = mBounds.mMinimum.Min(Joint);
            mBounds.mMaximum = mBounds.mMaximum.Max(Joint);
        }

        const float Radius{std::max(mBounds.mMinimum.Distance(mBounds.mMaximum) * 0.5f, (0.01_m).GetBase())};
        mSkeletonShapes = BuildBoneShapes(mSkeleton->GetBones(), Transforms, Radius);

        if (mMesh == nullptr) {
            for (const FBoneShape& Shape : mSkeletonShapes) {
                const FBox Bounds{Engine::GetEditorMesh(Shape.GetMesh()).GetBounds().Transformed(Shape.GetWorld())};

                mBounds.mMinimum = mBounds.mMinimum.Min(Bounds.mMinimum);
                mBounds.mMaximum = mBounds.mMaximum.Max(Bounds.mMaximum);
            }
        }

    }

    if (mMesh == nullptr) {
        return;
    }

    Engine::FPrimitiveSceneProxy Proxy{};
    Proxy.mId = mPrimitiveId;
    Proxy.mMesh = mMesh;
    Proxy.mBounds = mPose.GetRevision().IsValid() ? mPose.GetMeshData().GetBounds() : mMesh->GetBounds();
    Proxy.mMaterial = mMaterial;
    Proxy.mMaterials = mMaterials;

    if (mMaterial != nullptr) {
        Proxy.mWorld = mMaterialPreviewTransform;
        Proxy.mBounds = Proxy.mBounds.Transformed(Proxy.mWorld);
    }

    if (mPose.GetRevision().IsValid()) {
        Proxy.mSkeletalMesh = mSkeletalMesh;
        Proxy.mSkinMatrices = mPose.GetSkinMatrices();
        Proxy.mPoseRevision = mPose.GetRevision();

        // 천 섹션은 렌더러가 이 결과의 입자 위치로 정점을 옮겨 그립니다.
        if (!mClothSimData.empty()) {
            Proxy.mClothSimData = mClothSimData;
            Proxy.mClothBlendWeight = 1.0f;
            Proxy.mClothRevision = mClothRevision;
        }
    }

    mBounds = Proxy.mBounds;
    mScene.GetScene()->AddPrimitive(Proxy);
    mScene.Synchronize();
}

void FMeshViewerContent::BuildOverlay(Engine::FPrimitiveDrawInterface& Overlay, float) const {
    if (mSkeleton != nullptr && (mMesh == nullptr || mShowBones)) {
        for (const FBoneShape& Shape : mSkeletonShapes) {
            const FVector4 Color{Shape.GetMesh() == Engine::EEditorMesh::Sphere ? FVector4{0.88f, 0.92f, 1.0f, 1.0f} : FVector4{0.49f, 0.7f, 0.9f, 1.0f}};

            Overlay.DrawMesh({Shape.GetMesh(), Shape.GetWorld(), Color});
        }
    }
}

Asset::UStaticMesh* FMeshViewerContent::GetMesh() const {
    return mMesh;
}

Asset::USkeletalMesh* FMeshViewerContent::GetSkeletalMesh() const {
    return mSkeletalMesh;
}

Asset::USkeleton* FMeshViewerContent::GetSkeleton() const {
    return mSkeleton;
}

Engine::FSkeletalMeshPose& FMeshViewerContent::GetPose() {
    return mPose;
}

Core::TArray<CoreUObject::TObjectRef<Asset::UMaterial>>& FMeshViewerContent::GetMaterials() {
    return mMaterials;
}

bool FMeshViewerContent::SetSkeleton(Asset::USkeleton* Skeleton, bool ShowBones) {
    Engine::FSkeletalMeshPose Pose{};

    if (Skeleton != nullptr && (mSkeletalMesh == nullptr || !Pose.Reset(*mSkeletalMesh, *Skeleton) || mRenderer == nullptr || !mSkeletalMesh->CreateResources(static_cast<ID3D11Device*>(mRenderer->GetNativeDevice())))) {
        return false;
    }

    mSkeleton = Skeleton;
    mShowBones = ShowBones;
    mPose = std::move(Pose);
    Refresh();

    return true;
}

void FMeshViewerContent::Refresh() {
    mDirty = true;
    Synchronize();
}

/**
 * @brief 천 섹션을 그릴 천 시뮬레이션 결과를 넣고 미리 보기 프록시를 다시 만듭니다.
 */
void FMeshViewerContent::SetClothSimulationData(Core::TMap<Index_t, PhysicsRuntime::FClothSimulData> Data) {
    // 천을 새로 만들면 섹션 연결이 바뀌어 천 정점 버퍼가 비워지므로 다시 준비합니다.
    if (mSkeletalMesh != nullptr && mRenderer != nullptr && !Data.empty()) {
        mSkeletalMesh->CreateResources(static_cast<ID3D11Device*>(mRenderer->GetNativeDevice()));
    }

    mClothSimData = std::move(Data);
    mClothRevision = Core::FRevision::Next();
    Refresh();
}

/**
 * @brief 천 시뮬레이션 결과를 비워 천 섹션을 스키닝 위치로 그리게 합니다.
 */
void FMeshViewerContent::ClearClothSimulationData() {
    if (mClothSimData.empty()) {
        return;
    }

    mClothSimData.clear();
    mClothRevision = Core::FRevision::Next();
    Refresh();
}
