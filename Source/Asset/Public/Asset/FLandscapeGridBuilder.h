#pragma once

#include "Core/Types.h"

#include <memory>

namespace Asset {

	class UStaticMesh;

	// 서브섹션 LOD 하나의 인덱스 버퍼 구간
	struct FLandscapeLODSectionRange {
		Uint32 mFirstIndex{};
		Uint32 mIndexCount{};
		Uint32 mQuadsPerSide{};
		Uint32 mVerticesPerSide{};
	};

	// 공유 평면 서브섹션 그리드 메시와 LOD 구간 테이블
	struct FLandscapeSharedGrid {
		std::unique_ptr<Asset::UStaticMesh> mMesh{};
		Core::TArray<Asset::FLandscapeLODSectionRange> mLODRanges{};
		Uint32 mSubsectionSizeQuads{};
	};

	// 서브섹션 규격별 공유 평면 그리드 생성기
	class FLandscapeGridBuilder {
	public:
		// 유효한 서브섹션 쿼드 크기인지 검사
		static bool IsValidSubsectionSizeQuads(Uint32 SubsectionSizeQuads);

		// 서브섹션 크기에 대한 최대 LOD 계산
		static Uint32 ComputeMaxLOD(Uint32 SubsectionSizeQuads);

		// 모든 LOD가 패킹된 공유 평면 서브섹션 그리드 생성
		static bool BuildSharedSubsectionGrid(Uint32 SubsectionSizeQuads, Asset::FLandscapeSharedGrid& OutGrid);
	};

}
