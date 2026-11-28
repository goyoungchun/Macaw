#pragma once

#include "Core/Math/FVector4.h"
#include "Core/Types.h"

namespace Renderer {

	// 지형 정점 셰이더 변위 상수 버퍼
	struct alignas(16) FLandscapeVertexConstants {
		FVector4 mSubsectionParams{};
		FVector4 mHeightmapParams{};
		FVector4 mComponentParams{};
		FVector4 mLODParams{};
		Core::TStaticArray<Uint32, 4> mStitchParams{};
	};

	static_assert(sizeof(FLandscapeVertexConstants) == 80u);

	// 지형 픽셀 셰이더 멀티 레이어 상수 버퍼
	struct alignas(16) FLandscapePixelConstants {
		static constexpr Uint32 MaxLayers{8u};

		Core::TStaticArray<Uint32, 4> mLayerMeta{};
		FVector4 mWeightmapScaleBias{};
		Core::TStaticArray<FVector4, MaxLayers> mLayerBaseColor{};
		Core::TStaticArray<FVector4, MaxLayers> mLayerSpecularColor{};
		Core::TStaticArray<FVector4, MaxLayers> mLayerSurfaceParams{};
		Core::TStaticArray<FVector4, MaxLayers> mLayerUVAndChannel{};
	};

	// 드로 명령에 첨부하는 지형 서브섹션 정보
	struct FLandscapeDrawInfo {
		bool mIsLandscape{};
		Uint32 mSubX{};
		Uint32 mSubY{};
		Uint32 mLOD{};
		float mMorphFactor{};
		Uint32 mStitchMask{};
	};

}
