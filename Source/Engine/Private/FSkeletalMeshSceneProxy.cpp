#include "Engine/FSkeletalMeshSceneProxy.h"
#include "Asset/USkeletalMesh.h"

#include <utility>

using namespace Engine;

FSkeletalMeshSceneProxy::FSkeletalMeshSceneProxy(CoreUObject::TObjectRef<Asset::USkeletalMesh> SkeletalMesh, Core::TArray<FMatrix> SkinMatrices, Core::FRevision PoseRevision)
    : mSkeletalMesh{SkeletalMesh},
      mSkinMatrices{std::move(SkinMatrices)},
      mPoseRevision{PoseRevision} {
}

const FSkeletalMeshSceneProxy* FSkeletalMeshSceneProxy::FindSkinnedProxy(const FPrimitiveSceneProxy& Proxy) {
    return Proxy.GetVertexFactoryType() == EVertexFactoryType::Skinned ? dynamic_cast<const FSkeletalMeshSceneProxy*>(&Proxy) : nullptr;
}

EVertexFactoryType FSkeletalMeshSceneProxy::GetVertexFactoryType() const {
    // 스키닝에 사용할 메시가 프록시에 연결되어 있을 때만 본 가중치를 처리하는 경로로 보냅니다.
    return mPoseRevision.IsValid() && mSkeletalMesh != nullptr ? EVertexFactoryType::Skinned : EVertexFactoryType::Static;
}

Asset::USkeletalMesh* FSkeletalMeshSceneProxy::GetSkeletalMesh() const {
    return mSkeletalMesh.Get();
}

const Core::TArray<FMatrix>& FSkeletalMeshSceneProxy::GetSkinMatrices() const {
    return mSkinMatrices;
}

Core::FRevision FSkeletalMeshSceneProxy::GetPoseRevision() const {
    return mPoseRevision;
}

void FSkeletalMeshSceneProxy::SetClothData(const Core::TMap<Index_t, PhysicsRuntime::FClothSimulData>& SimData, float BlendWeight, Core::FRevision Revision) {
    mClothSimData = SimData;
    mClothBlendWeight = BlendWeight;
    mClothRevision = Revision;
}

const Core::TMap<Index_t, PhysicsRuntime::FClothSimulData>& FSkeletalMeshSceneProxy::GetClothSimData() const {
    return mClothSimData;
}

float FSkeletalMeshSceneProxy::GetClothBlendWeight() const {
    return mClothBlendWeight;
}

Core::FRevision FSkeletalMeshSceneProxy::GetClothRevision() const {
    return mClothRevision;
}
