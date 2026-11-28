#pragma once

#include "Asset/FLandscapeGridBuilder.h"
#include "Core/FRevision.h"
#include "Core/Guid.h"
#include "Core/Math/FBox.h"
#include "Core/Math/FVector2.h"
#include "Core/Types.h"
#include "CoreUObject/TObjectRef.h"

#include <memory>

namespace Asset {
	class UMaterial;
	class UTexture2D;
}

namespace Engine {

	// 같은 지형 액터에 속한 컴포넌트들이 공유하는 렌더 정보
	struct FLandscapeSharedRenderData {
		Core::FGuid mLandscapeId{};

		Uint32 mSubsectionSizeQuads{63u};
		Uint32 mNumSubsections{1u};
		Uint32 mComponentSizeQuads{63u};

		Uint32 mComponentCountX{1u};
		Uint32 mComponentCountY{1u};

		Uint32 mTotalResolutionX{64u};
		Uint32 mTotalResolutionY{64u};

		float mQuadSize{1.0f};
		float mHeightScale{512.0f};

		Uint32 mMaxLOD{0u};
		float mLODDistanceFactor{1.0f};
		Core::FRevision mDataRevision{};

		CoreUObject::TObjectRef<Asset::UTexture2D> mHeightmapTexture{};
		Core::TArray<Asset::FLandscapeLODSectionRange> mLODRanges{};
	};

	// 컴포넌트 하나의 활성 레이어 채널 매핑
	struct FLandscapeLayerAllocation {
		CoreUObject::TObjectRef<Asset::UMaterial> mMaterial{};
		FVector2 mUVScale{0.25f, 0.25f};
		Uint8 mWeightmapIndex{};
		Uint8 mWeightmapChannel{};
		bool mWeightBlended{true};
	};

	// 지형 컴포넌트 하나가 렌더 프록시에 채우는 데이터
	struct FLandscapeComponentRenderData {
		std::shared_ptr<const Engine::FLandscapeSharedRenderData> mShared{};

		Uint32 mComponentIndexX{};
		Uint32 mComponentIndexY{};
		Uint32 mSectionBaseX{};
		Uint32 mSectionBaseY{};

		Core::TArray<FBox> mLocalSubsectionBounds{};
		Core::TArray<FBox> mSubsectionBounds{};
		Core::TArray<CoreUObject::TObjectRef<Asset::UTexture2D>> mWeightmapTextures{};
		Core::TArray<Engine::FLandscapeLayerAllocation> mLayerAllocations{};
	};

}
