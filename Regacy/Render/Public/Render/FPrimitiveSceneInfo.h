#pragma once
#include "RenderCore/FPrimitiveSceneProxy.h"
#include "RenderCore/FMeshBatch.h"
#include "Render/FMeshDrawCommand.h"
#include "Core/Base/TCachedValue.h"
#include "Render/FRenderAssetStamp.h"
#include "Asset/FLODSettings.h"
#include "Core/Types.h"

struct FPrimitiveSceneInfo {
    FObjectHandle mComponentHandle{};
    FObjectHandle mOwnerHandle{};
    DirectX::BoundingSphere mWorldSphereBounds{};
    DirectX::BoundingOrientedBox mWorldOBB{};
    DirectX::BoundingBox mWorldAABB{};
    bool mActive{};
    bool mCullable{};
    bool mSky{};
    std::unique_ptr<FPrimitiveSceneProxy> mProxy{};
    TCachedValue<TArray<FMeshBatch>, FRenderAssetStamp> mStaticMeshes{};
    Core::TStaticArray<TArray<Uint32>, GLODCount> mCachedCommandsByLOD{};
    Uint32 mAvailableLODMask{};
};
