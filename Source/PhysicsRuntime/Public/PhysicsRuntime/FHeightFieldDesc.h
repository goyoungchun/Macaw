#pragma once

#include "Core/Math/FQuat.h"
#include "Core/Math/FVector.h"
#include "Core/Types.h"

#include <PxPhysicsAPI.h>

namespace PhysicsRuntime {

	// 하이트필드 셀별 물리 재질 계수
	struct FHeightFieldMaterialParams {
		float mStaticFriction{0.5f};
		float mDynamicFriction{0.5f};
		float mRestitution{0.2f};
	};

	// PhysX 하이트필드 충돌 형상 생성 서술자
	struct FHeightFieldDesc {
		Uint32 mRows{};
		Uint32 mColumns{};
		float mRowScale{1.0f};
		float mColumnScale{1.0f};
		float mHeightScale{1.0f};
		FVector mLocalPosition{};
		FQuat mLocalRotation{};
		Core::TArray<physx::PxHeightFieldSample> mSamples{};
		Core::TArray<PhysicsRuntime::FHeightFieldMaterialParams> mMaterials{};
		bool mDrivableSurface{true};
	};

}
