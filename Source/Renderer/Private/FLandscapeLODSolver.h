#pragma once

#include "Core/Guid.h"
#include "Core/GuidHash.h"
#include "Core/Math/FVector.h"
#include "Core/Types.h"

namespace Renderer {

	class FScene;

	// 서브섹션 하나의 계산된 LOD와 스티칭 상태
	struct FLandscapeSectionLODState {
		Uint32 mLOD{};
		float mMorphFactor{};
		Uint32 mStitchMask{};
	};

	// 장면 내 모든 지형 액터의 서브섹션 LOD와 경계 스티칭 계산기
	class FLandscapeLODSolver {
	public:
		// 카메라 위치 기준으로 지형별 서브섹션 LOD와 이웃 스티칭 마스크 계산
		void Solve(const Renderer::FScene& Scene, const FVector& CameraLocation);

		// 지정한 지형 서브섹션의 LOD 상태 조회
		Renderer::FLandscapeSectionLODState GetSectionState(const Core::FGuid& LandscapeId, Uint32 GlobalSubX, Uint32 GlobalSubY) const;

		void Clear();

	private:
		struct FLandscapeGridState {
			Uint32 mSubCountX{};
			Uint32 mSubCountY{};
			Uint32 mMaxLOD{};
			Core::TArray<Renderer::FLandscapeSectionLODState> mSections{};
			Core::TArray<Uint8> mPresent{};
		};

	private:
		Core::TMap<Core::FGuid, FLandscapeGridState, Core::FGuidHash> mLandscapes{};
	};

}
